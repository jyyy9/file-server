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
#include "common/common.h"
#include "common/logger.h"

using namespace fileserver;

muduo::net::EventLoop* g_loop = nullptr;

int main() {
    Logger::Instance().SetLevel(LogLevel::kInfo);

    // ── 配置（环境变量优先，未设置时用默认值）──────────────────
    const std::string zk_hosts       = GetEnv("FS_ZK_HOSTS", "127.0.0.1:2181");
    const std::string storage        = GetEnv("FS_STORAGE_ROOT", "./storage/data");
    // 注册到 ZooKeeper 的地址：容器/多机部署时必须设置成本机可达地址
    const std::string advertise_host = GetEnv("FS_ADVERTISE_HOST", "127.0.0.1");

    storage::StorageManager st(storage);

    // ── RPC 服务（控制面：只传输元数据）─────────────────────────
    muduo::net::EventLoop loop;
    g_loop = &loop;
    rpc::RpcServer server(&loop, muduo::net::InetAddress(9003),
                           "StorageService", 2, 2);

    server.RegisterBinaryMethod("WriteChunk", [&](const nlohmann::json& args, const std::string& data) {
        const std::string filepath = args.at("filepath");
        const int64_t user_id = args.at("user_id");
        const int64_t offset = args.at("offset");
        if (user_id <= 0 || offset < 0 || data.empty() ||
            args.at("size").get<int64_t>() != static_cast<int64_t>(data.size()) ||
            filepath.compare(0, std::to_string(user_id).size() + 1, std::to_string(user_id) + "/") != 0 ||
            filepath.find("..") != std::string::npos || filepath.find('\\') != std::string::npos ||
            !st.CreateUserDir(user_id))
            return nlohmann::json{{"code", -1}, {"msg", "无效的存储写入请求"}};
        const int64_t written = st.WriteChunk(filepath, data.data(), data.size(), offset);
        return nlohmann::json{{"code", written == static_cast<int64_t>(data.size()) ? 0 : -1},
                              {"written", written}};
    });

    server.RegisterMethod("GetFileSize", [&](const nlohmann::json& args) {
        const int64_t size = st.GetFileSize(args.at("filepath"));
        return nlohmann::json{{"code", size >= 0 ? 0 : -1}, {"size", size}};
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

    server.Start();

    // ── ZooKeeper 注册 ──────────────────────────────────────────
    rpc::ZkClient zk(zk_hosts);
    if (zk.Connect()) {
        zk.Register("/FileServer/StorageService", advertise_host, 9003);
    }

    std::cout << "\nStorageService 已启动, RPC=" << advertise_host << ":9003\n\n";
    loop.loop();
    return 0;
}
