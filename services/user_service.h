#ifndef FILESERVER_SERVICES_USER_SERVICE_H_
#define FILESERVER_SERVICES_USER_SERVICE_H_

#include <string>
#include <memory>

#include "database/user_dao.h"
#include "database/user.h"
#include "common/common.h"

namespace fileserver {
namespace services {

// ── 用户服务 ─────────────────────────────────────────────────────
//
// 业务逻辑层: 不直接操作数据库，通过 UserDAO 访问。
//
// 功能:
//   Register(username, password)  → 返回 token，失败返回空
//   Login(username, password)     → 返回 token，失败返回空
//   VerifyToken(token)            → 返回 user_id，失败返回 -1
//
// 密码暂时明文存储，后续可扩展 bcrypt/scrypt
class UserService {
public:
    explicit UserService(database::UserDAO* dao);
    ~UserService() = default;

    FILESERVER_DISALLOW_COPY_AND_MOVE(UserService);

    // ── 注册 ─────────────────────────────────────────────────────
    // 成功返回 token 字符串，失败返回空
    // 失败原因: 用户名已存在、数据库错误
    std::string Register(const std::string& username,
                         const std::string& password);

    // ── 登录 ─────────────────────────────────────────────────────
    // 成功返回 token 字符串，失败返回空
    // 失败原因: 用户不存在、密码错误
    std::string Login(const std::string& username,
                      const std::string& password);

    // ── 验证 token ───────────────────────────────────────────────
    // 成功返回 user_id，失败返回 -1
    int64_t VerifyToken(const std::string& token);

    // ── 获取用户信息 ─────────────────────────────────────────────
    database::UserPtr GetUserById(int64_t user_id);
    database::UserPtr GetUserByUsername(const std::string& username);

private:
    // 生成随机 token（时间戳 + 随机数）
    std::string GenerateToken();

    database::UserDAO* dao_;  // 不持有所有权
};

}  // namespace services
}  // namespace fileserver

#endif  // FILESERVER_SERVICES_USER_SERVICE_H_
