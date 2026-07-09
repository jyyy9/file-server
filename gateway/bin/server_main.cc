#include <iostream>
#include <csignal>
#include <string>

#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>

#include "gateway/network/tcp_server_wrapper.h"
#include "gateway/handler/business_processor.h"
#include "gateway/handler/request_router.h"
#include "protocol/encoder.h"

#include "database/mysql_pool.h"
#include "database/user_dao.h"
#include "database/file_dao.h"
#include "storage/storage_manager.h"
#include "services/user_service.h"
#include "services/file_service.h"

#include "common/logger.h"
#include "common/common.h"

using namespace fileserver;
using namespace fileserver::gateway::network;
using namespace fileserver::gateway::handler;

// 全局指针，用于信号处理
muduo::net::EventLoop* g_loop = nullptr;

void SignalHandler(int sig) {
    LOG_INFO("收到信号 " + std::to_string(sig) + "，退出...");
    if (g_loop) g_loop->quit();
}

// ── 构造响应 ─────────────────────────────────────────────────────
static protocol::Message MakeResponse(const protocol::Message& req,
                                       int code, const std::string& msg,
                                       const nlohmann::json& data = nlohmann::json::object(),
                                       const std::string& binary = "") {
    protocol::Message resp;
    resp.header.msg_type   = static_cast<uint32_t>(protocol::MessageType::kResponse);
    resp.header.request_id = req.header.request_id;

    nlohmann::json j;
    j["code"] = code;
    j["msg"]  = msg;
    if (!data.empty()) j["data"] = data;
    resp.SetJson(j);
    resp.binary_data = binary;
    resp.header.data_length = static_cast<uint32_t>(binary.size());
    return resp;
}

// ── Token 提取 ────────────────────────────────────────────────────
static std::string GetToken(const protocol::Message& req) {
    auto j = req.GetJson();
    return j.value("token", "");
}

static int64_t GetUserIdFromToken(const std::string& token,
                                   services::UserService* user_svc) {
    return user_svc->VerifyToken(token);
}

// ═══════════════════════════════════════════════════════════════════
int main() {
    Logger::Instance().SetLevel(LogLevel::kDebug);

    // ── 数据库 ──────────────────────────────────────────────────
    const char* db_host = std::getenv("FS_DB_HOST") ?: "127.0.0.1";
    const char* db_user = std::getenv("FS_DB_USER") ?: "root";
    const char* db_pass = std::getenv("FS_DB_PASS") ?: "123456";
    const char* db_name = std::getenv("FS_DB_NAME") ?: "fileserver";
    const char* storage_root = std::getenv("FS_STORAGE_ROOT") ?: "./storage/data";

    database::MysqlPool db_pool(db_host, 3306, db_user, db_pass, db_name, 8);
    storage::StorageManager storage(storage_root);

    // ── DAO ──────────────────────────────────────────────────────
    database::UserDAO user_dao(&db_pool);
    database::FileDAO file_dao(&db_pool);

    // ── Service ──────────────────────────────────────────────────
    services::UserService user_svc(&user_dao);
    services::FileService file_svc(&file_dao, &storage);

    // ── 路由注册 ─────────────────────────────────────────────────
    RequestRouter router;

    router.Register("register", [&](const protocol::Message& req) {
        auto j = req.GetJson();
        std::string token = user_svc.Register(
            j.value("username", ""), j.value("password", ""));
        if (token.empty())
            return MakeResponse(req, -1, "注册失败: 用户名已存在或参数错误");
        return MakeResponse(req, 0, "ok", {{"token", token}});
    });

    router.Register("login", [&](const protocol::Message& req) {
        auto j = req.GetJson();
        std::string token = user_svc.Login(
            j.value("username", ""), j.value("password", ""));
        if (token.empty())
            return MakeResponse(req, -1, "登录失败: 用户名或密码错误");
        return MakeResponse(req, 0, "ok", {{"token", token}});
    });

    router.Register("upload_start", [&](const protocol::Message& req) {
        auto j = req.GetJson();
        int64_t uid = GetUserIdFromToken(GetToken(req), &user_svc);
        if (uid <= 0) return MakeResponse(req, -1, "认证失败");

        auto result = file_svc.UploadStart(uid, j.value("filename", ""),
            j.value("filesize", 0LL), j.value("md5", ""));
        if (result.file_id <= 0)
            return MakeResponse(req, -1, "上传初始化失败");
        nlohmann::json d;
        d["file_id"]   = result.file_id;
        d["offset"]    = result.offset;
        d["is_resume"] = result.is_resume;
        return MakeResponse(req, 0, "ok", d);
    });

    router.Register("upload_data", [&](const protocol::Message& req) {
        int64_t uid = GetUserIdFromToken(GetToken(req), &user_svc);
        if (uid <= 0) return MakeResponse(req, -1, "认证失败");

        auto j = req.GetJson();
        int64_t written = file_svc.UploadData(j["file_id"], uid,
            req.binary_data, j.value("offset", 0LL));
        if (written < 0)
            return MakeResponse(req, -1, "数据写入失败");
        return MakeResponse(req, 0, "ok", {{"written", written}});
    });

    router.Register("upload_finalize", [&](const protocol::Message& req) {
        int64_t uid = GetUserIdFromToken(GetToken(req), &user_svc);
        if (uid <= 0) return MakeResponse(req, -1, "认证失败");
        auto j = req.GetJson();
        bool ok = file_svc.UploadFinalize(j["file_id"], uid);
        return ok ? MakeResponse(req, 0, "上传完成")
                  : MakeResponse(req, -1, "上传完成失败");
    });

    router.Register("download", [&](const protocol::Message& req) {
        int64_t uid = GetUserIdFromToken(GetToken(req), &user_svc);
        if (uid <= 0) return MakeResponse(req, -1, "认证失败");
        auto j = req.GetJson();
        auto info = file_svc.GetFileInfo(j["file_id"]);
        if (!info || info->user_id != uid)
            return MakeResponse(req, -1, "文件不存在");
        return MakeResponse(req, 0, "ok",
            {{"filesize", info->filesize}, {"filename", info->filename},
             {"md5", info->md5}, {"status", info->status}});
    });

    router.Register("download_chunk", [&](const protocol::Message& req) {
        int64_t uid = GetUserIdFromToken(GetToken(req), &user_svc);
        if (uid <= 0) return MakeResponse(req, -1, "认证失败");
        auto j = req.GetJson();
        std::string data = file_svc.DownloadChunk(
            j["file_id"], uid, j.value("offset", 0LL), j.value("size", 0UL));
        if (data.empty())
            return MakeResponse(req, -1, "读取失败或文件末尾");
        return MakeResponse(req, 0, "ok",
            {{"size", static_cast<int64_t>(data.size())}}, data);
    });

    router.Register("list", [&](const protocol::Message& req) {
        int64_t uid = GetUserIdFromToken(GetToken(req), &user_svc);
        if (uid <= 0) return MakeResponse(req, -1, "认证失败");
        auto files = file_svc.QueryFiles(uid);
        nlohmann::json arr = nlohmann::json::array();
        for (auto& f : files) {
            nlohmann::json item;
            item["id"] = f->id; item["filename"] = f->filename;
            item["filesize"] = f->filesize; item["status"] = f->status;
            item["upload_size"] = f->upload_size;
            arr.push_back(item);
        }
        return MakeResponse(req, 0, "ok", {{"files", arr}});
    });

    router.Register("delete", [&](const protocol::Message& req) {
        int64_t uid = GetUserIdFromToken(GetToken(req), &user_svc);
        if (uid <= 0) return MakeResponse(req, -1, "认证失败");
        auto j = req.GetJson();
        bool ok = file_svc.Delete(j["file_id"], uid);
        return ok ? MakeResponse(req, 0, "删除成功")
                  : MakeResponse(req, -1, "删除失败");
    });

    // ── 网络层 ──────────────────────────────────────────────────
    muduo::net::EventLoop loop;
    g_loop = &loop;

    muduo::net::InetAddress listen_addr(kDefaultPort);
    TcpServerWrapper server(&loop, listen_addr, "FileServer", 4);
    ThreadPool pool(8);

    // 连接 BusinessProcessor（IO → Task → Pool → Router）
    BusinessProcessor processor(&pool);
    processor.SetTaskHandler(
        [&router](NetworkTask& task, int64_t /*delay_ms*/) {
            auto response = router.Route(task.request);
            std::string wire = protocol::Encoder::Encode(response);
            if (task.conn && task.conn->connected())
                task.conn->send(wire);
        });

    server.SetMessageCallback(processor.GetMessageCallback());

    // ── 启动 ────────────────────────────────────────────────────
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    server.Start();

    std::cout << "\n============================================\n"
              << "  FileServer 已启动\n"
              << "  端口: " << kDefaultPort << "\n"
              << "  存储: " << storage_root << "\n"
              << "  Ctrl+C 停止\n"
              << "============================================\n\n";

    loop.loop();

    LOG_INFO("FileServer 已退出");
    return 0;
}
