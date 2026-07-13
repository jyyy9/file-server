#include "client/file_client.h"

#include <chrono>
#include <thread>
#include <fstream>
#include <cstdio>
#include <openssl/evp.h>

namespace fileserver {
namespace client {

// ── 计算文件 MD5（OpenSSL 3.0 EVP API）──────────────────────────
std::string FileClient::ComputeFileMD5(const std::string& filepath) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_md5(), nullptr);
    std::ifstream fs(filepath, std::ios::binary);
    char buf[8192];
    while (fs.read(buf, sizeof(buf)) || fs.gcount() > 0)
        EVP_DigestUpdate(ctx, buf, fs.gcount());
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    EVP_DigestFinal_ex(ctx, digest, &digest_len);
    EVP_MD_CTX_free(ctx);
    char hex[33];
    for (unsigned int i = 0; i < digest_len; ++i)
        snprintf(hex + i*2, 3, "%02x", digest[i]);
    return hex;
}

// ── 构造/析构 ────────────────────────────────────────────────────
FileClient::FileClient()
    : loop_(loop_thread_.startLoop())
{
    tcp_client_.reset(new muduo::net::TcpClient(loop_,
        muduo::net::InetAddress(), "FileClient"));
}

FileClient::~FileClient() {
    Disconnect();
    loop_thread_.getLoop()->quit();
}

// ── 连接 ─────────────────────────────────────────────────────────
bool FileClient::Connect(const std::string& host, int port) {
    // 先断开旧连接
    if (connection_) {
        connection_->shutdown();
        connection_.reset();
    }
    tcp_client_.reset(new muduo::net::TcpClient(loop_,
        muduo::net::InetAddress(host, port), "FileClient"));
    tcp_client_->setConnectionCallback(
        std::bind(&FileClient::OnConnection, this, std::placeholders::_1));
    tcp_client_->setMessageCallback(
        std::bind(&FileClient::OnMessage, this,
                  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    tcp_client_->connect();

    // 等待连接建立（最多 3 秒）
    for (int i = 0; i < 30 && !connected_; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    return connected_;
}

void FileClient::Disconnect() {
    connected_ = false;
    if (connection_) {
        connection_->shutdown();
    }
}

bool FileClient::IsConnected() const { return connected_; }

// ── 连接回调 ─────────────────────────────────────────────────────
void FileClient::OnConnection(const TcpConnectionPtr& conn) {
    if (conn->connected()) {
        connection_ = conn;
        connected_ = true;
    } else {
        connection_.reset();
        connected_ = false;
    }
}

// ── 消息回调（IO线程）───────────────────────────────────────────
void FileClient::OnMessage(const TcpConnectionPtr&,
                            muduo::net::Buffer* buf, muduo::Timestamp) {
    std::string raw = buf->retrieveAllAsString();
    auto msgs = decoder_.Feed(raw);

    for (auto& msg : msgs) {
        ClientResponse resp;
        auto j = msg.GetJson();
        resp.code = j.value("code", -1);
        resp.msg  = j.value("msg", "");
        resp.data = j.value("data", nlohmann::json::object());
        resp.binary_data = msg.binary_data;

        ResolvePending(msg.header.request_id, resp);
    }
}

// ── 请求 ID ──────────────────────────────────────────────────────
uint32_t FileClient::NextRequestId() {
    return next_request_id_.fetch_add(1);
}

void FileClient::SetPending(uint32_t req_id, std::promise<ClientResponse>* promise) {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_requests_[req_id] = promise;
}

void FileClient::ResolvePending(uint32_t req_id, const ClientResponse& resp) {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    auto it = pending_requests_.find(req_id);
    if (it != pending_requests_.end()) {
        it->second->set_value(resp);
        pending_requests_.erase(it);
    }
}

// ── 同步请求 ─────────────────────────────────────────────────────
ClientResponse FileClient::SendRequest(const nlohmann::json& req_json,
                                         int timeout_ms) {
    return SendRequestWithBinary(req_json, "", timeout_ms);
}

ClientResponse FileClient::SendRequestWithBinary(const nlohmann::json& req_json,
                                                   const std::string& binary,
                                                   int timeout_ms) {
    if (!connection_) return {-1, "未连接", {}, {}};

    protocol::Message msg;
    msg.header.msg_type   = static_cast<uint32_t>(protocol::MessageType::kRequest);
    msg.header.request_id = NextRequestId();
    msg.SetJson(req_json);
    msg.binary_data = binary;
    msg.header.data_length = static_cast<uint32_t>(binary.size());

    std::string wire = protocol::Encoder::Encode(msg);

    std::promise<ClientResponse> promise;
    auto future = promise.get_future();
    SetPending(msg.header.request_id, &promise);

    loop_->runInLoop([this, wire]() {
        if (connection_) connection_->send(wire);
    });

    auto status = future.wait_for(std::chrono::milliseconds(timeout_ms));
    if (status == std::future_status::timeout) {
        // 超时清理
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            pending_requests_.erase(msg.header.request_id);
        }
        return {-2, "请求超时", {}, {}};
    }

    return future.get();
}

// ═══════════════════════════════════════════════════════════════════
// 高级 API
// ═══════════════════════════════════════════════════════════════════

ClientResponse FileClient::Register(const std::string& user, const std::string& pass) {
    nlohmann::json j;
    j["cmd"] = "register";
    j["username"] = user;
    j["password"] = pass;
    return SendRequest(j);
}

ClientResponse FileClient::Login(const std::string& user, const std::string& pass) {
    nlohmann::json j;
    j["cmd"] = "login";
    j["username"] = user;
    j["password"] = pass;
    auto resp = SendRequest(j);
    if (resp.ok() && resp.data.contains("token")) {
        token_ = resp.data["token"];
    }
    return resp;
}

ClientResponse FileClient::UploadStart(const std::string& token,
                                         const std::string& filename,
                                         int64_t filesize, const std::string& md5) {
    nlohmann::json j;
    j["cmd"] = "upload_start";
    j["token"] = token;
    j["filename"] = filename;
    j["filesize"] = filesize;
    j["md5"] = md5;
    return SendRequest(j);
}

ClientResponse FileClient::UploadData(const std::string& token,
                                        int64_t file_id, int64_t offset,
                                        const std::string& chunk) {
    nlohmann::json j;
    j["cmd"] = "upload_data";
    j["token"] = token;
    j["file_id"] = file_id;
    j["offset"] = offset;
    j["size"] = static_cast<int64_t>(chunk.size());
    return SendRequestWithBinary(j, chunk);
}

ClientResponse FileClient::UploadFinalize(const std::string& token, int64_t file_id) {
    nlohmann::json j;
    j["cmd"] = "upload_finalize";
    j["token"] = token;
    j["file_id"] = file_id;
    return SendRequest(j);
}

ClientResponse FileClient::ListFiles(const std::string& token) {
    nlohmann::json j;
    j["cmd"] = "list";
    j["token"] = token;
    return SendRequest(j);
}

ClientResponse FileClient::DeleteFile(const std::string& token, int64_t file_id) {
    nlohmann::json j;
    j["cmd"] = "delete";
    j["token"] = token;
    j["file_id"] = file_id;
    return SendRequest(j);
}

// ── 上传文件（完整流程）──────────────────────────────────────────
ClientResponse FileClient::UploadFile(const std::string& token,
                                        const std::string& local_path,
                                        ProgressCallback progress) {
    // 1. 计算文件信息
    std::ifstream fs(local_path, std::ios::binary | std::ios::ate);
    if (!fs) return {-1, "无法打开文件: " + local_path, {}, {}};
    int64_t filesize = fs.tellg();
    fs.close();

    std::string md5 = ComputeFileMD5(local_path);
    std::string filename = local_path;
    size_t pos = filename.find_last_of("/\\");
    if (pos != std::string::npos) filename = filename.substr(pos + 1);

    // 2. upload_start
    auto start_resp = UploadStart(token, filename, filesize, md5);
    if (!start_resp.ok()) return start_resp;

    int64_t file_id = start_resp.data["file_id"];
    int64_t offset  = start_resp.data.value("offset", 0LL);

    // 3. 分块上传
    fs.open(local_path, std::ios::binary);
    fs.seekg(offset);

    const int64_t kChunkSize = 4 * 1024 * 1024;
    std::string buffer;
    while (offset < filesize) {
        size_t size = std::min(kChunkSize, static_cast<int64_t>(filesize - offset));
        buffer.resize(size);
        fs.read(&buffer[0], size);

        auto data_resp = UploadData(token, file_id, offset, buffer);
        if (!data_resp.ok()) {
            fs.close();
            return data_resp;
        }

        offset += size;
        if (progress) progress(offset, filesize);
    }
    fs.close();

    // 4. finalize
    return UploadFinalize(token, file_id);
}

// ── 下载文件（完整流程）──────────────────────────────────────────
ClientResponse FileClient::DownloadFile(const std::string& token,
                                          int64_t file_id,
                                          const std::string& save_path,
                                          ProgressCallback progress) {
    // 1. 请求下载
    nlohmann::json j;
    j["cmd"] = "download";
    j["token"] = token;
    j["file_id"] = file_id;

    // 2. 先获取文件信息
    auto info_resp = SendRequest(j);
    if (!info_resp.ok()) return info_resp;

    int64_t filesize = info_resp.data.value("filesize", 0LL);
    if (filesize == 0) return {-1, "文件不存在", {}, {}};

    // 3. 分块下载
    std::ofstream out(save_path, std::ios::binary);
    int64_t offset = 0;
    const int64_t kChunkSize = 4 * 1024 * 1024;

    while (offset < filesize) {
        size_t size = std::min(kChunkSize, static_cast<int64_t>(filesize - offset));

        nlohmann::json req;
        req["cmd"] = "download_chunk";
        req["token"] = token;
        req["file_id"] = file_id;
        req["offset"] = offset;
        req["size"] = static_cast<int64_t>(size);

        auto resp = SendRequest(req);
        if (!resp.ok()) { out.close(); return resp; }

        out.write(resp.binary_data.data(), resp.binary_data.size());
        offset += resp.binary_data.size();

        if (progress) progress(offset, filesize);
    }

    out.close();
    return {0, "ok", {{"filesize", filesize}}, {}};
}

}  // namespace client
}  // namespace fileserver
