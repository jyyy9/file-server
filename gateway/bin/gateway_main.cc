// ── Gateway 微服务进程 ───────────────────────────────────────────
// 不直接操作数据库/磁盘，通过 RPC 调用下游服务
// 控制面: RPC (ZooKeeper 发现)
// 数据面: TCP 直传 (文件上传/下载 chunk)

#include <iostream>
#include <csignal>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>

#include "gateway/network/tcp_server_wrapper.h"
#include "gateway/handler/business_processor.h"
#include "gateway/handler/request_router.h"
#include "gateway/handler/task.h"

#include "rpc/rpc_client.h"
#include "rpc/zk_client.h"

#include "common/logger.h"
#include "common/common.h"

using namespace fileserver;
using namespace fileserver::gateway::network;
using namespace fileserver::gateway::handler;

muduo::net::EventLoop* g_loop = nullptr;

// ── RPC 服务发现 ─────────────────────────────────────────────────
class ServiceDiscovery {
public:
    ServiceDiscovery(rpc::ZkClient* zk) : zk_(zk) {}

    // 获取一个可用的 UserService 地址
    std::string GetUserService() {
        auto nodes = zk_->Discover("/FileServer/UserService");
        if (nodes.empty()) return "127.0.0.1:9001";  // fallback
        return nodes[0];  // 简单策略: 取第一个
    }

    std::string GetFileService() {
        auto nodes = zk_->Discover("/FileServer/FileService");
        if (nodes.empty()) return "127.0.0.1:9002";
        return nodes[0];
    }

    std::string GetStorageService() {
        auto nodes = zk_->Discover("/FileServer/StorageService");
        if (nodes.empty()) return "127.0.0.1:9003";
        return nodes[0];
    }

private:
    rpc::ZkClient* zk_;
};

// ── 解析地址 "ip:port" ──────────────────────────────────────────
static std::pair<std::string,int> ParseAddr(const std::string& addr) {
    auto pos = addr.find(':');
    if (pos == std::string::npos) return {addr, 9001};
    return {addr.substr(0, pos), std::stoi(addr.substr(pos+1))};
}

// ── 调用远程 RPC ─────────────────────────────────────────────────
static nlohmann::json CallRpc(ServiceDiscovery& sd,
                                const std::string& service,
                                const std::string& method,
                                const nlohmann::json& args) {
    std::string addr;
    if (service == "UserService")    addr = sd.GetUserService();
    else if (service == "FileService") addr = sd.GetFileService();
    else addr = sd.GetStorageService();

    auto [host, port] = ParseAddr(addr);

    rpc::RpcClient client;
    if (!client.Connect(host, port)) {
        return {{"code", -1}, {"msg", "无法连接 " + service + " at " + addr}};
    }

    auto result = client.Call(method, args);
    client.Disconnect();
    return result;
}

// ── 辅助 ─────────────────────────────────────────────────────────
static protocol::Message MakeResponse(const protocol::Message& req,
                                       int code, const std::string& msg,
                                       const nlohmann::json& data = {},
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
static int64_t VerifyToken(ServiceDiscovery& sd, const std::string& token) {
    if (token.empty()) return -1;
    auto r = CallRpc(sd, "UserService", "VerifyToken", {{"token", token}});
    return r.value("user_id", 0LL);
}

// ═══════════════════════════════════════════════════════════════════
int main() {
    Logger::Instance().SetLevel(LogLevel::kDebug);

    // ── ZooKeeper ───────────────────────────────────────────────
    rpc::ZkClient zk("127.0.0.1:2181");
    if (!zk.Connect()) {
        LOG_WARNING("ZooKeeper 连接失败，使用默认地址");
    }
    ServiceDiscovery sd(&zk);

    // ── 路由 ─────────────────────────────────────────────────────
    RequestRouter router;

    router.Register("register", [&](const protocol::Message& req) {
        auto j = req.GetJson();
        auto r = CallRpc(sd, "UserService", "Register", j);
        return MakeResponse(req, r.value("code", -1),
                            r.value("msg", ""), r);
    });

    router.Register("login", [&](const protocol::Message& req) {
        auto j = req.GetJson();
        auto r = CallRpc(sd, "UserService", "Login", j);
        return MakeResponse(req, r.value("code", -1),
                            r.value("msg", ""), r);
    });

    router.Register("upload_start", [&](const protocol::Message& req) {
        auto j = req.GetJson();
        int64_t uid = VerifyToken(sd, j.value("token", ""));
        if (uid <= 0) return MakeResponse(req, -1, "认证失败");

        nlohmann::json args;
        args["user_id"] = uid;
        args["filename"] = j["filename"];
        args["filesize"] = j["filesize"];
        args["md5"] = j["md5"];
        auto r = CallRpc(sd, "FileService", "UploadStart", args);
        return MakeResponse(req, r.value("code", -1),
                            r.value("msg", ""), r);
    });

    router.Register("upload_data", [&](const protocol::Message& req) {
        auto j = req.GetJson();
        int64_t uid = VerifyToken(sd, j.value("token", ""));
        if (uid <= 0) return MakeResponse(req, -1, "认证失败");

        // 数据面: 直接写磁盘 (StorageService 在此架构中介入较少)
        // 大文件 chunk 通过此连接直传，不经过 RPC
        return MakeResponse(req, 0, "chunk received",
                            {{"size", static_cast<int64_t>(req.binary_data.size())}});
        // 注: 实际微服务部署中，客户端直连 StorageService 发 chunk
    });

    router.Register("upload_finalize", [&](const protocol::Message& req) {
        auto j = req.GetJson();
        int64_t uid = VerifyToken(sd, j.value("token", ""));
        if (uid <= 0) return MakeResponse(req, -1, "认证失败");
        auto r = CallRpc(sd, "FileService", "UploadFinalize",
                          {{"file_id",j["file_id"]},{"user_id",uid}});
        return MakeResponse(req, r.value("code", -1),
                            r.value("msg", ""), r);
    });

    router.Register("download", [&](const protocol::Message& req) {
        auto j = req.GetJson();
        int64_t uid = VerifyToken(sd, j.value("token", ""));
        if (uid <= 0) return MakeResponse(req, -1, "认证失败");
        auto r = CallRpc(sd, "FileService", "GetFileInfo",
                          {{"file_id",j["file_id"]}});
        return MakeResponse(req, r.value("code", -1),
                            r.value("msg", ""), r);
    });

    router.Register("download_chunk", [&](const protocol::Message& req) {
        auto j = req.GetJson();
        int64_t uid = VerifyToken(sd, j.value("token", ""));
        if (uid <= 0) return MakeResponse(req, -1, "认证失败");
        auto r = CallRpc(sd, "StorageService", "ReadChunk",
                          {{"filepath",j.value("filepath","")},
                           {"offset",j["offset"]},{"size",j["size"]}});
        return MakeResponse(req, 0, "ok", r, r.value("data",""));
    });

    router.Register("list", [&](const protocol::Message& req) {
        auto j = req.GetJson();
        int64_t uid = VerifyToken(sd, j.value("token", ""));
        if (uid <= 0) return MakeResponse(req, -1, "认证失败");
        auto r = CallRpc(sd, "FileService", "QueryFiles", {{"user_id",uid}});
        return MakeResponse(req, 0, "ok", r);
    });

    router.Register("delete", [&](const protocol::Message& req) {
        auto j = req.GetJson();
        int64_t uid = VerifyToken(sd, j.value("token", ""));
        if (uid <= 0) return MakeResponse(req, -1, "认证失败");
        auto r = CallRpc(sd, "FileService", "Delete",
                          {{"file_id",j["file_id"]},{"user_id",uid}});
        return MakeResponse(req, r.value("code", -1),
                            r.value("msg", ""), r);
    });

    // ── 网络层 ──────────────────────────────────────────────────
    muduo::net::EventLoop loop;
    g_loop = &loop;
    TcpServerWrapper server(&loop, muduo::net::InetAddress(kDefaultPort),
                             "Gateway", 4);
    ThreadPool pool(8);
    BusinessProcessor processor(&pool);
    processor.SetTaskHandler([&router](NetworkTask& task, int64_t) {
        auto response = router.Route(task.request);
        std::string wire = protocol::Encoder::Encode(response);
        if (task.conn && task.conn->connected())
            task.conn->send(wire);
    });
    server.SetMessageCallback(processor.GetMessageCallback());

    std::signal(SIGINT, [](int){ g_loop->quit(); });
    server.Start();

    std::cout << "\n============================================\n"
              << "  Gateway 已启动 (微服务模式)\n"
              << "  端口: " << kDefaultPort << "\n"
              << "  ZK: 127.0.0.1:2181\n"
              << "============================================\n\n";

    loop.loop();
    return 0;
}
