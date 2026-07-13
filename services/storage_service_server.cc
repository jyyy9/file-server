// ── StorageService 微服务进程 ─────────────────────────────────────
// 向 ZooKeeper 注册 /FileServer/StorageService
// 监听 RPC 端口 9003
// 数据面: 客户端直连 StorageService 上传/下载 chunk

#include <csignal>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include "rpc/rpc_server.h"
#include "rpc/zk_client.h"
#include "storage/storage_manager.h"
#include "common/logger.h"

using namespace fileserver;

muduo::net::EventLoop* g_loop = nullptr;

int main() {
    Logger::Instance().SetLevel(LogLevel::kInfo);

    const char* zk_hosts = "127.0.0.1:2181";
    const char* storage  = "./storage/data";

    storage::StorageManager st(storage);

    // ── RPC 服务（控制面：只传输元数据）─────────────────────────
    muduo::net::EventLoop loop;
    g_loop = &loop;
    rpc::RpcServer server(&loop, muduo::net::InetAddress(9003),
                           "StorageService", 2, 2);

    server.RegisterMethod("WriteChunk", [&](const nlohmann::json& args) {
        // 注意: chunk 数据不在这个 RPC 里传输
        // 大文件数据通过独立 TCP 数据面直传
        int64_t written = st.WriteChunk(
            args["filepath"],
            args.value("data","").c_str(),
            args.value("size", 0UL),
            args["offset"]);
        nlohmann::json r;
        r["code"]    = (written >= 0) ? 0 : -1;
        r["written"] = written;
        return r;
    });

    server.RegisterMethod("ReadChunk", [&](const nlohmann::json& args) {
        std::string data = st.ReadChunk(
            args["filepath"],
            args["offset"],
            args.value("size", 0UL));
        nlohmann::json r;
        r["code"] = 0;
        r["data"] = data;
        r["size"] = static_cast<int64_t>(data.size());
        return r;
    });

    server.RegisterMethod("DeleteFile", [&](const nlohmann::json& args) {
        bool ok = st.DeleteFile(args["filepath"]);
        nlohmann::json r;
        r["code"] = ok ? 0 : -1;
        return r;
    });

    server.RegisterMethod("GetFileSize", [&](const nlohmann::json& args) {
        int64_t sz = st.GetFileSize(args["filepath"]);
        nlohmann::json r;
        r["code"] = 0;
        r["size"] = sz;
        return r;
    });

    server.Start();

    // ── ZooKeeper 注册 ──────────────────────────────────────────
    rpc::ZkClient zk(zk_hosts);
    if (zk.Connect()) {
        zk.Register("/FileServer/StorageService", "127.0.0.1", 9003);
    }

    std::cout << "\nStorageService 已启动, RPC=9003\n\n";
    loop.loop();
    return 0;
}
