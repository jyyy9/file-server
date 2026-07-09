#ifndef FILESERVER_CLIENT_FILE_CLIENT_H_
#define FILESERVER_CLIENT_FILE_CLIENT_H_

#include <string>
#include <functional>
#include <future>
#include <mutex>
#include <unordered_map>
#include <atomic>
#include <memory>

#include <muduo/net/TcpClient.h>
#include <muduo/net/EventLoopThread.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/TcpConnection.h>
#include <muduo/net/Buffer.h>
#include <nlohmann/json.hpp>

#include "protocol/encoder.h"
#include "protocol/decoder.h"
#include "protocol/message.h"
#include "common/common.h"

namespace fileserver {
namespace client {

// ── 请求响应 ─────────────────────────────────────────────────────
struct ClientResponse {
    int         code = 0;
    std::string msg;
    nlohmann::json data;
    std::string binary_data;  // 下载/上传等返回的二进制数据
    bool        ok() const { return code == 0; }
};

// ── 文件客户端 ───────────────────────────────────────────────────
//
// 基于 muduo TcpClient + Encoder/Decoder，提供同步请求-响应模式。
// 内部维护 EventLoop 线程用于网络IO，主线程通过 promise/future 同步等待。
class FileClient {
public:
    using TcpConnectionPtr = muduo::net::TcpConnectionPtr;
    using ProgressCallback = std::function<void(int64_t done, int64_t total)>;

    FileClient();
    ~FileClient();

    FILESERVER_DISALLOW_COPY_AND_MOVE(FileClient);

    // ── 连接 ─────────────────────────────────────────────────────
    bool Connect(const std::string& host, int port);
    void Disconnect();
    bool IsConnected() const;

    // ── 同步请求（阻塞当前线程，等待响应或超时）─────────────────
    ClientResponse SendRequest(const nlohmann::json& req_json,
                                int timeout_ms = 10000);
    ClientResponse SendRequestWithBinary(const nlohmann::json& req_json,
                                          const std::string& binary,
                                          int timeout_ms = 10000);

    // ── 高级API ──────────────────────────────────────────────────
    ClientResponse Register(const std::string& user, const std::string& pass);
    ClientResponse Login(const std::string& user, const std::string& pass);

    ClientResponse UploadStart(const std::string& token, const std::string& filename,
                                int64_t filesize, const std::string& md5);
    ClientResponse UploadData(const std::string& token, int64_t file_id,
                               int64_t offset, const std::string& chunk);
    ClientResponse UploadFinalize(const std::string& token, int64_t file_id);

    // 上传文件（完整流程，含进度）
    ClientResponse UploadFile(const std::string& token, const std::string& local_path,
                               ProgressCallback progress = nullptr);

    // 下载文件（完整流程，含进度）
    ClientResponse DownloadFile(const std::string& token, int64_t file_id,
                                 const std::string& save_path,
                                 ProgressCallback progress = nullptr);

    ClientResponse ListFiles(const std::string& token);
    ClientResponse DeleteFile(const std::string& token, int64_t file_id);

    // ── 静态工具 ─────────────────────────────────────────────────
    static std::string ComputeFileMD5(const std::string& filepath);

    // ── 状态 ─────────────────────────────────────────────────────
    std::string GetToken() const { return token_; }
    void SetToken(const std::string& t) { token_ = t; }

private:
    void OnConnection(const TcpConnectionPtr& conn);
    void OnMessage(const TcpConnectionPtr& conn,
                   muduo::net::Buffer* buf, muduo::Timestamp);

    uint32_t NextRequestId();
    void SetPending(uint32_t req_id, std::promise<ClientResponse>* promise);
    void ResolvePending(uint32_t req_id, const ClientResponse& resp);
    void CleanupTimeout(uint32_t req_id);

    // ── 网络组件 ─────────────────────────────────────────────────
    muduo::net::EventLoopThread loop_thread_;
    muduo::net::EventLoop* loop_;
    std::unique_ptr<muduo::net::TcpClient> tcp_client_;
    TcpConnectionPtr connection_;
    protocol::Decoder decoder_;

    // ── 请求追踪 ─────────────────────────────────────────────────
    std::mutex pending_mutex_;
    std::unordered_map<uint32_t, std::promise<ClientResponse>*> pending_requests_;
    std::atomic<uint32_t> next_request_id_{1};

    // ── 会话 ─────────────────────────────────────────────────────
    std::string token_;
    bool connected_{false};
};

}  // namespace client
}  // namespace fileserver

#endif  // FILESERVER_CLIENT_FILE_CLIENT_H_
