#include "database/user_dao.h"
#include "common/logger.h"

#include <mysql/mysql.h>
#include <cstring>
#include <ctime>
#include <sstream>
#include <iomanip>

namespace fileserver {
namespace database {

UserDAO::UserDAO(MysqlPool* pool)
    : pool_(pool)
{
    LOG_INFO("UserDAO 初始化完成");
}

// ── 插入用户 ─────────────────────────────────────────────────────
int64_t UserDAO::Insert(const User& user) {
    auto conn = pool_->GetConnection();
    if (!conn) {
        LOG_ERROR("UserDAO::Insert - 无法获取数据库连接");
        return -1;
    }

    // Prepared Statement: 防 SQL 注入
    const char* sql =
        "INSERT INTO user (username, password, token, create_time) "
        "VALUES (?, ?, ?, NOW())";

    MYSQL_STMT* stmt = mysql_stmt_init(conn.get());
    if (!stmt) {
        LOG_ERROR("mysql_stmt_init 失败");
        return -1;
    }

    if (mysql_stmt_prepare(stmt, sql, std::strlen(sql)) != 0) {
        LOG_ERROR(std::string("mysql_stmt_prepare 失败: ") + mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }

    // 绑定参数
    MYSQL_BIND bind[3];
    std::memset(bind, 0, sizeof(bind));

    // username
    unsigned long username_len = user.username.size();
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer       = const_cast<char*>(user.username.c_str());
    bind[0].buffer_length = username_len;
    bind[0].length       = &username_len;
    bind[0].is_null      = nullptr;

    // password
    unsigned long password_len = user.password.size();
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer       = const_cast<char*>(user.password.c_str());
    bind[1].buffer_length = password_len;
    bind[1].length       = &password_len;
    bind[1].is_null      = nullptr;

    // token
    unsigned long token_len = user.token.size();
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer       = const_cast<char*>(user.token.c_str());
    bind[2].buffer_length = token_len;
    bind[2].length       = &token_len;
    bind[2].is_null      = nullptr;

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        LOG_ERROR(std::string("mysql_stmt_bind_param 失败: ") + mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }

    // 执行
    if (mysql_stmt_execute(stmt) != 0) {
        std::string err = mysql_stmt_error(stmt);
        // 重复用户名 (Duplicate entry)
        if (err.find("Duplicate") != std::string::npos) {
            LOG_WARNING("用户名已存在: " + user.username);
            mysql_stmt_close(stmt);
            return -2;  // -2 = 重复
        }
        LOG_ERROR(std::string("mysql_stmt_execute 失败: ") + err);
        mysql_stmt_close(stmt);
        return -1;
    }

    int64_t id = mysql_stmt_insert_id(stmt);
    mysql_stmt_close(stmt);

    LOG_INFO("用户注册成功: id=" + std::to_string(id) + ", username=" + user.username);

    return id;
}

// ── 按用户名查找 ─────────────────────────────────────────────────
UserPtr UserDAO::FindByUsername(const std::string& username) {
    auto conn = pool_->GetConnection();
    if (!conn) return nullptr;

    const char* sql = "SELECT id, username, password, token, create_time "
                      "FROM user WHERE username = ?";

    MYSQL_STMT* stmt = mysql_stmt_init(conn.get());
    if (!stmt) return nullptr;

    if (mysql_stmt_prepare(stmt, sql, std::strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        return nullptr;
    }

    unsigned long len = username.size();
    MYSQL_BIND bind[1];
    std::memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer       = const_cast<char*>(username.c_str());
    bind[0].buffer_length = len;
    bind[0].length       = &len;

    if (mysql_stmt_bind_param(stmt, bind) != 0 ||
        mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        return nullptr;
    }

    // 绑定结果
    int64_t id = 0;
    char name_buf[65] = {0};
    char pass_buf[129] = {0};
    char token_buf[129] = {0};
    char time_buf[32] = {0};

    MYSQL_BIND result[5];
    std::memset(result, 0, sizeof(result));
    unsigned long name_len, pass_len, token_len, time_len;
    bool is_null[5] = {0};

    result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result[0].buffer       = &id;

    result[1].buffer_type = MYSQL_TYPE_STRING;
    result[1].buffer       = name_buf;
    result[1].buffer_length = sizeof(name_buf) - 1;
    result[1].length       = &name_len;

    result[2].buffer_type = MYSQL_TYPE_STRING;
    result[2].buffer       = pass_buf;
    result[2].buffer_length = sizeof(pass_buf) - 1;
    result[2].length       = &pass_len;

    result[3].buffer_type = MYSQL_TYPE_STRING;
    result[3].buffer       = token_buf;
    result[3].buffer_length = sizeof(token_buf) - 1;
    result[3].length       = &token_len;

    result[4].buffer_type = MYSQL_TYPE_STRING;
    result[4].buffer       = time_buf;
    result[4].buffer_length = sizeof(time_buf) - 1;
    result[4].length       = &time_len;

    mysql_stmt_bind_result(stmt, result);
    mysql_stmt_store_result(stmt);

    UserPtr user;
    if (mysql_stmt_fetch(stmt) == 0) {
        name_buf[name_len] = '\0';
        pass_buf[pass_len] = '\0';
        token_buf[token_len] = '\0';
        time_buf[time_len] = '\0';

        user = std::make_shared<User>();
        user->id = id;
        user->username = name_buf;
        user->password = pass_buf;
        user->token = token_buf;
        user->create_time = time_buf;
    }

    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);

    return user;
}

// ── 按ID查找 ────────────────────────────────────────────────────
UserPtr UserDAO::FindById(int64_t id) {
    auto conn = pool_->GetConnection();
    if (!conn) return nullptr;

    const char* sql = "SELECT id, username, password, token, create_time "
                      "FROM user WHERE id = ?";

    MYSQL_STMT* stmt = mysql_stmt_init(conn.get());
    if (!stmt) return nullptr;

    mysql_stmt_prepare(stmt, sql, std::strlen(sql));

    MYSQL_BIND bind[1];
    std::memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer       = &id;

    if (mysql_stmt_bind_param(stmt, bind) != 0 ||
        mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        return nullptr;
    }

    int64_t rid = 0;
    char name_buf[65] = {0}, pass_buf[129] = {0};
    char token_buf[129] = {0}, time_buf[32] = {0};
    unsigned long name_len, pass_len, token_len, time_len;

    MYSQL_BIND result[5];
    std::memset(result, 0, sizeof(result));
    result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result[0].buffer       = &rid;
    result[1].buffer_type = MYSQL_TYPE_STRING;
    result[1].buffer       = name_buf;
    result[1].buffer_length = sizeof(name_buf) - 1;
    result[1].length       = &name_len;
    result[2].buffer_type = MYSQL_TYPE_STRING;
    result[2].buffer       = pass_buf;
    result[2].buffer_length = sizeof(pass_buf) - 1;
    result[2].length       = &pass_len;
    result[3].buffer_type = MYSQL_TYPE_STRING;
    result[3].buffer       = token_buf;
    result[3].buffer_length = sizeof(token_buf) - 1;
    result[3].length       = &token_len;
    result[4].buffer_type = MYSQL_TYPE_STRING;
    result[4].buffer       = time_buf;
    result[4].buffer_length = sizeof(time_buf) - 1;
    result[4].length       = &time_len;

    mysql_stmt_bind_result(stmt, result);
    mysql_stmt_store_result(stmt);

    UserPtr user;
    if (mysql_stmt_fetch(stmt) == 0) {
        name_buf[name_len] = '\0';
        pass_buf[pass_len] = '\0';
        token_buf[token_len] = '\0';
        time_buf[time_len] = '\0';
        user = std::make_shared<User>();
        user->id = rid;
        user->username = name_buf;
        user->password = pass_buf;
        user->token = token_buf;
        user->create_time = time_buf;
    }

    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
    return user;
}

// ── 更新 token ───────────────────────────────────────────────────
bool UserDAO::UpdateToken(int64_t user_id, const std::string& token) {
    auto conn = pool_->GetConnection();
    if (!conn) return false;

    const char* sql = "UPDATE user SET token = ? WHERE id = ?";

    MYSQL_STMT* stmt = mysql_stmt_init(conn.get());
    if (!stmt) return false;

    mysql_stmt_prepare(stmt, sql, std::strlen(sql));

    unsigned long token_len = token.size();
    MYSQL_BIND bind[2];
    std::memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer       = const_cast<char*>(token.c_str());
    bind[0].buffer_length = token_len;
    bind[0].length       = &token_len;
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer       = &user_id;

    bool ok = (mysql_stmt_bind_param(stmt, bind) == 0 &&
              mysql_stmt_execute(stmt) == 0);

    mysql_stmt_close(stmt);
    return ok;
}

// ── 按 token 查找 ────────────────────────────────────────────────
UserPtr UserDAO::FindByToken(const std::string& token) {
    auto conn = pool_->GetConnection();
    if (!conn) return nullptr;

    const char* sql = "SELECT id, username, password, token, create_time "
                      "FROM user WHERE token = ? AND token != ''";

    MYSQL_STMT* stmt = mysql_stmt_init(conn.get());
    if (!stmt) return nullptr;

    mysql_stmt_prepare(stmt, sql, std::strlen(sql));

    unsigned long len = token.size();
    MYSQL_BIND bind[1];
    std::memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer       = const_cast<char*>(token.c_str());
    bind[0].buffer_length = len;
    bind[0].length       = &len;

    if (mysql_stmt_bind_param(stmt, bind) != 0 ||
        mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        return nullptr;
    }

    int64_t id = 0;
    char name_buf[65] = {0}, pass_buf[129] = {0};
    char token_buf[129] = {0}, time_buf[32] = {0};
    unsigned long name_len, pass_len, token_len, time_len;

    MYSQL_BIND result[5];
    std::memset(result, 0, sizeof(result));
    result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result[0].buffer       = &id;
    result[1].buffer_type = MYSQL_TYPE_STRING;
    result[1].buffer       = name_buf;
    result[1].buffer_length = sizeof(name_buf) - 1;
    result[1].length       = &name_len;
    result[2].buffer_type = MYSQL_TYPE_STRING;
    result[2].buffer       = pass_buf;
    result[2].buffer_length = sizeof(pass_buf) - 1;
    result[2].length       = &pass_len;
    result[3].buffer_type = MYSQL_TYPE_STRING;
    result[3].buffer       = token_buf;
    result[3].buffer_length = sizeof(token_buf) - 1;
    result[3].length       = &token_len;
    result[4].buffer_type = MYSQL_TYPE_STRING;
    result[4].buffer       = time_buf;
    result[4].buffer_length = sizeof(time_buf) - 1;
    result[4].length       = &time_len;

    mysql_stmt_bind_result(stmt, result);
    mysql_stmt_store_result(stmt);

    UserPtr user;
    if (mysql_stmt_fetch(stmt) == 0) {
        name_buf[name_len] = '\0';
        pass_buf[pass_len] = '\0';
        token_buf[token_len] = '\0';
        time_buf[time_len] = '\0';
        user = std::make_shared<User>();
        user->id = id;
        user->username = name_buf;
        user->password = pass_buf;
        user->token = token_buf;
        user->create_time = time_buf;
    }

    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
    return user;
}

}  // namespace database
}  // namespace fileserver
