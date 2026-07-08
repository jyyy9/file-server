#include "services/user_service.h"
#include "common/logger.h"

#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace fileserver {
namespace services {

UserService::UserService(database::UserDAO* dao)
    : dao_(dao)
{
    LOG_INFO("UserService 初始化完成");
}

// ── 生成 token ───────────────────────────────────────────────────
// 格式: 时间戳(16进制) + 随机数(16进制) → 32字符
std::string UserService::GenerateToken() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()).count();

    // 随机数
    static std::mt19937_64 rng(
        static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    uint64_t random = rng();

    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(16) << ms
        << std::setw(16) << random;
    return oss.str();
}

// ── 注册 ─────────────────────────────────────────────────────────
std::string UserService::Register(const std::string& username,
                                   const std::string& password) {
    // 参数校验
    if (username.empty() || password.empty()) {
        LOG_WARNING("注册失败: 用户名或密码为空");
        return "";
    }
    if (username.size() > 64 || password.size() > 128) {
        LOG_WARNING("注册失败: 用户名或密码过长");
        return "";
    }

    // 检查用户名是否已存在
    auto existing = dao_->FindByUsername(username);
    if (existing) {
        LOG_WARNING("注册失败: 用户名已存在 - " + username);
        return "";
    }

    // 生成 token
    std::string token = GenerateToken();

    // 插入数据库
    database::User user(username, password);
    user.token = token;

    int64_t id = dao_->Insert(user);
    if (id <= 0) {
        LOG_ERROR("注册失败: 数据库插入错误");
        return "";
    }

    LOG_INFO("用户注册成功: id=" + std::to_string(id) + ", username=" + username);

    return token;
}

// ── 登录 ─────────────────────────────────────────────────────────
std::string UserService::Login(const std::string& username,
                                const std::string& password) {
    // 查找用户
    auto user = dao_->FindByUsername(username);
    if (!user) {
        LOG_WARNING("登录失败: 用户不存在 - " + username);
        return "";
    }

    // 验证密码
    if (user->password != password) {
        LOG_WARNING("登录失败: 密码错误 - " + username);
        return "";
    }

    // 生成新 token 并更新
    std::string token = GenerateToken();
    if (!dao_->UpdateToken(user->id, token)) {
        LOG_ERROR("登录失败: 更新 token 错误");
        return "";
    }

    LOG_INFO("用户登录成功: id=" + std::to_string(user->id) + ", username=" + username);

    return token;
}

// ── 验证 token ───────────────────────────────────────────────────
int64_t UserService::VerifyToken(const std::string& token) {
    if (token.empty()) {
        return -1;
    }

    auto user = dao_->FindByToken(token);
    if (!user) {
        return -1;  // token 不存在或已过期
    }

    return user->id;
}

// ── 获取用户信息 ─────────────────────────────────────────────────
database::UserPtr UserService::GetUserById(int64_t user_id) {
    return dao_->FindById(user_id);
}

database::UserPtr UserService::GetUserByUsername(const std::string& username) {
    return dao_->FindByUsername(username);
}

}  // namespace services
}  // namespace fileserver
