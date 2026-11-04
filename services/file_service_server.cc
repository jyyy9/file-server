// ── FileService 微服务进程 ───────────────────────────────────────
// 向 ZooKeeper 注册 /FileServer/FileService
// 监听 RPC 端口 9002

#include <csignal>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include "rpc/rpc_server.h"
#include "rpc/zk_client.h"
#include "database/mysql_pool.h"
#include "database/file_dao.h"
#include "services/file_service.h"
#include "storage/storage_manager.h"
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
    const std::string storage        = GetEnv("FS_STORAGE_ROOT", "./storage/data");
    // 注册到 ZooKeeper 的地址：容器/多机部署时必须设置成本机可达地址
    const std::string advertise_host = GetEnv("FS_ADVERTISE_HOST", "127.0.0.1");

    // ── 数据库 + 存储 ───────────────────────────────────────────
    database::MysqlPool pool(db_host, db_port, db_user, db_pass, db_name, 4);
    database::FileDAO file_dao(&pool);
    storage::StorageManager st(storage);
    services::FileService file_svc(&file_dao, &st);

    // ── RPC 服务 ────────────────────────────────────────────────
    muduo::net::EventLoop loop;
    g_loop = &loop;
    rpc::RpcServer server(&loop, muduo::net::InetAddress(9002),
                           "FileService", 2, 4);

    server.RegisterMethod("UploadStart", [&](const nlohmann::json& args) {
        auto result = file_svc.UploadStart(
            args.value("user_id", 0LL),
            args.value("filename", ""),
            args.value("filesize", 0LL),
            args.value("md5", ""));
        nlohmann::json r;
        r["code"]     = (result.file_id > 0) ? 0 : -1;
        r["file_id"]  = result.file_id;
        r["offset"]   = result.offset;
        r["is_resume"] = result.is_resume;
        return r;
    });

    server.RegisterMethod("UploadFinalize", [&](const nlohmann::json& args) {
        bool ok = file_svc.UploadFinalize(
            args["file_id"], args["user_id"]);
        nlohmann::json r;
        r["code"] = ok ? 0 : -1;
        return r;
    });

    server.RegisterMethod("GetFileInfo", [&](const nlohmann::json& args) {
        auto info = file_svc.GetFileInfo(args["file_id"]);
        nlohmann::json r;
        if (info) {
            r["code"] = 0;
            r["filesize"] = info->filesize;
            r["filename"] = info->filename;
            r["md5"] = info->md5;
            r["status"] = info->status;
        } else {
            r["code"] = -1;
            r["msg"] = "文件不存在";
        }
        return r;
    });

    server.RegisterMethod("QueryFiles", [&](const nlohmann::json& args) {
        auto files = file_svc.QueryFiles(args["user_id"]);
        nlohmann::json r, arr = nlohmann::json::array();
        for (auto& f : files) {
            arr.push_back({{"id",f->id},{"filename",f->filename},
                           {"filesize",f->filesize},{"status",f->status}});
        }
        r["code"] = 0;
        r["files"] = arr;
        return r;
    });

    server.RegisterMethod("Delete", [&](const nlohmann::json& args) {
        bool ok = file_svc.Delete(args["file_id"], args["user_id"]);
        nlohmann::json r;
        r["code"] = ok ? 0 : -1;
        return r;
    });

    server.Start();

    // ── ZooKeeper 注册 ──────────────────────────────────────────
    rpc::ZkClient zk(zk_hosts);
    if (zk.Connect()) {
        zk.Register("/FileServer/FileService", advertise_host, 9002);
    }

    std::cout << "\nFileService 已启动, RPC=" << advertise_host << ":9002\n\n";
    loop.loop();
    return 0;
}
