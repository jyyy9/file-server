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
#include "common/logger.h"

using namespace fileserver;

muduo::net::EventLoop* g_loop = nullptr;

int main() {
    Logger::Instance().SetLevel(LogLevel::kInfo);

    const char* db_host  = "127.0.0.1";
    const char* db_user  = "root";
    const char* db_pass  = "123456";
    const char* db_name  = "fileserver";
    const char* zk_hosts = "127.0.0.1:2181";

    // ── 数据库 ──────────────────────────────────────────────────
    database::MysqlPool pool(db_host, 3306, db_user, db_pass, db_name, 4);
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
        zk.Register("/FileServer/UserService", "127.0.0.1", 9001);
    }

    std::cout << "\nUserService 已启动, RPC=9001\n\n";
    loop.loop();
    return 0;
}
