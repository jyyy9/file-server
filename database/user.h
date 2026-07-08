#ifndef FILESERVER_DATABASE_USER_H_
#define FILESERVER_DATABASE_USER_H_

#include <cstdint>
#include <string>

namespace fileserver {
namespace database {

// ── 用户实体 ─────────────────────────────────────────────────────
struct User {
    int64_t id = 0;
    std::string username;
    std::string password;       // 暂时明文，后续扩展 hash
    std::string token;
    std::string create_time;

    User() = default;
    User(const std::string& name, const std::string& pwd)
        : username(name), password(pwd) {}
};

using UserPtr = std::shared_ptr<User>;

}  // namespace database
}  // namespace fileserver

#endif  // FILESERVER_DATABASE_USER_H_
