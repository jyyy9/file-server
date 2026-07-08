#ifndef FILESERVER_DATABASE_USER_DAO_H_
#define FILESERVER_DATABASE_USER_DAO_H_

#include <string>
#include <memory>

#include "database/user.h"
#include "database/mysql_pool.h"
#include "common/common.h"

namespace fileserver {
namespace database {

// ── 用户数据访问对象 ─────────────────────────────────────────────
//
// 封装所有 user 表的 SQL 操作。
// Service 层不直接写 SQL，通过 DAO 访问数据库。
//
// 使用 MySQL C API 的 prepared statement 防止 SQL 注入。
class UserDAO {
public:
    explicit UserDAO(MysqlPool* pool);
    ~UserDAO() = default;

    FILESERVER_DISALLOW_COPY_AND_MOVE(UserDAO);

    // ── CRUD 操作 ────────────────────────────────────────────────

    // 插入新用户，返回自增 ID，失败返回 -1
    int64_t Insert(const User& user);

    // 按用户名查找
    UserPtr FindByUsername(const std::string& username);

    // 按ID查找
    UserPtr FindById(int64_t id);

    // 更新用户 token
    bool UpdateToken(int64_t user_id, const std::string& token);

    // 按 token 查找用户
    UserPtr FindByToken(const std::string& token);

private:
    // 从 MYSQL_RES 中提取一行构造 User 对象
    UserPtr RowToUser(MYSQL_RES* result);

    MysqlPool* pool_;  // 不持有所有权
};

}  // namespace database
}  // namespace fileserver

#endif  // FILESERVER_DATABASE_USER_DAO_H_
