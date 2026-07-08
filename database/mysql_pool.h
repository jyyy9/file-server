#ifndef FILESERVER_DATABASE_MYSQL_POOL_H_
#define FILESERVER_DATABASE_MYSQL_POOL_H_

#include <mysql/mysql.h>

#include <string>
#include <queue>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <cstdint>

#include "common/common.h"

namespace fileserver {
namespace database {

// ── MySQL 连接池 ─────────────────────────────────────────────────
//
// RAII 管理: 通过 shared_ptr 自定义删除器自动归还连接到池。
//
// 使用方式:
//   MysqlPool pool("127.0.0.1", 3306, "root", "password", "fileserver", 8);
//   auto conn = pool.GetConnection();        // 获取连接 (shared_ptr<MYSQL>)
//   mysql_query(conn.get(), "SELECT ...");   // 直接用 MySQL C API
//   conn.reset();                            // 手动归还（析构时自动归还）
//
// 线程安全: mutex + condition_variable
class MysqlPool {
public:
    // host:     数据库地址
    // port:     端口
    // user:     用户名
    // password: 密码
    // db:       数据库名
    // pool_size: 连接池大小
    MysqlPool(const std::string& host, int port,
              const std::string& user, const std::string& password,
              const std::string& db, size_t pool_size);
    ~MysqlPool();

    FILESERVER_DISALLOW_COPY_AND_MOVE(MysqlPool);

    // 获取一个连接 (shared_ptr，最后持有者析构时自动归还到池)
    std::shared_ptr<MYSQL> GetConnection();

    // 当前可用连接数
    size_t AvailableCount();

private:
    // 归还一个连接到池（由 shared_ptr 自定义删除器调用）
    void ReturnConnection(MYSQL* conn);

    // 创建一个新连接
    MYSQL* CreateConnection();

    std::queue<MYSQL*> pool_;
    std::mutex mutex_;
    std::condition_variable cond_;

    std::string host_;
    int port_;
    std::string user_;
    std::string password_;
    std::string db_;
    size_t pool_size_;
    size_t total_created_{0};
};

}  // namespace database
}  // namespace fileserver

#endif  // FILESERVER_DATABASE_MYSQL_POOL_H_
