// ── UserService 微服务进程 ───────────────────────────────────────
// 向 ZooKeeper 注册 /FileServer/UserService
// 监听 RPC 端口 9001

#include <csignal>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include "rpc/rpc_server.h"
#include "rpc/zk_client.h"
#include "database/mysql_pool.h"
#include "database/user_dao.h"
#include "services/user_service.h"
#include "common/common.h"
#include "common/logger.h"

using namespace fileserver;

muduo::net::EventLoop* g_loop = nullptr;

int main() {
    Logger::Instance().SetLevel(LogLevel::kInfo);

    // ── 配置（环境变量优先，未设置时用默认值）──────────────────
    const std::string db_host        = GetEnv("FS_DB_HOST", "127.0.0.1");
    const int         db_port        = GetEnvInt("FS_DB_PORT", 3306);
    const std::string db_user        = GetEnv("FS_DB_USER", "root");
    const std::string db_pass        = GetEnv("FS_DB_PASS", "123456");
    const std::string db_name        = GetEnv("FS_DB_NAME", "fileserver");
    const std::string zk_hosts       = GetEnv("FS_ZK_HOSTS", "127.0.0.1:2181");
    // 注册到 ZooKeeper 的地址：容器/多机部署时必须设置成本机可达地址
    const std::string advertise_host = GetEnv("FS_ADVERTISE_HOST", "127.0.0.1");

    // ── 数据库 ──────────────────────────────────────────────────
    database::MysqlPool pool(db_host, db_port, db_user, db_pass, db_name, 4);
    database::UserDAO user_dao(&pool);
    services::UserService user_svc(&user_dao);

    // ── RPC 服务 ────────────────────────────────────────────────
    muduo::net::EventLoop loop;
    g_loop = &loop;
    rpc::RpcServer server(&loop, muduo::net::InetAddress(9001),
                           "UserService", 2, 4);

    server.RegisterMethod("Register", [&](const nlohmann::json& args) {
        std::string token = user_svc.Register(
            args.value("username",""), args.value("password",""));
        nlohmann::json r;
        r["code"] = token.empty() ? -1 : 0;
        if (!token.empty()) r["token"] = token;
        else r["msg"] = "注册失败";
        return r;
    });

    server.RegisterMethod("Login", [&](const nlohmann::json& args) {
        std::string token = user_svc.Login(
            args.value("username",""), args.value("password",""));
        nlohmann::json r;
        r["code"] = token.empty() ? -1 : 0;
        if (!token.empty()) r["token"] = token;
        else r["msg"] = "登录失败";
        return r;
    });

    server.RegisterMethod("VerifyToken", [&](const nlohmann::json& args) {
        int64_t uid = user_svc.VerifyToken(args.value("token",""));
        nlohmann::json r;
        r["code"] = (uid > 0) ? 0 : -1;
        r["user_id"] = uid;
        return r;
    });

    server.Start();

    // ── ZooKeeper 注册 ──────────────────────────────────────────
    rpc::ZkClient zk(zk_hosts);
    if (zk.Connect()) {
        zk.Register("/FileServer/UserService", advertise_host, 9001);
    }

    std::cout << "\nUserService 已启动, RPC=" << advertise_host << ":9001\n\n";
    loop.loop();
    return 0;
}
