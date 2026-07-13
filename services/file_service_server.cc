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
    const char* storage  = "./storage/data";

    // ── 数据库 + 存储 ───────────────────────────────────────────
    database::MysqlPool pool(db_host, 3306, db_user, db_pass, db_name, 4);
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
        zk.Register("/FileServer/FileService", "127.0.0.1", 9002);
    }

    std::cout << "\nFileService 已启动, RPC=9002\n\n";
    loop.loop();
    return 0;
}
