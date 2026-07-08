#include <iostream>
#include <cassert>
#include <cstring>

#include "database/mysql_pool.h"
#include "database/user_dao.h"
#include "services/user_service.h"
#include "common/logger.h"

using namespace fileserver;
using namespace fileserver::database;
using namespace fileserver::services;

// ── 数据库配置 ───────────────────────────────────────────────────
const char* kDbHost     = "127.0.0.1";
const int   kDbPort     = 3306;
const char* kDbUser     = "root";
const char* kDbPassword = "123456";
const char* kDbName     = "fileserver";

// ── 建表 ─────────────────────────────────────────────────────────
static void CreateTable(MysqlPool* pool) {
    auto conn = pool->GetConnection();
    if (!conn) {
        std::cerr << "无法获取数据库连接" << std::endl;
        return;
    }

    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS user (
            id          INT PRIMARY KEY AUTO_INCREMENT,
            username    VARCHAR(64)  NOT NULL UNIQUE,
            password    VARCHAR(128) NOT NULL,
            token       VARCHAR(128) DEFAULT '',
            create_time DATETIME DEFAULT CURRENT_TIMESTAMP
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
    )";

    if (mysql_query(conn.get(), sql) != 0) {
        std::cerr << "建表失败: " << mysql_error(conn.get()) << std::endl;
    } else {
        std::cout << "user 表已就绪" << std::endl;
    }
}

// ── 清理测试数据 ─────────────────────────────────────────────────
static void Cleanup(MysqlPool* pool) {
    auto conn = pool->GetConnection();
    if (conn) {
        mysql_query(conn.get(), "DELETE FROM user WHERE username LIKE 'test_%'");
    }
}

// ═══════════════════════════════════════════════════════════════════
int main() {
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "  FileServer – 用户认证模块测试" << std::endl;
    std::cout << std::string(60, '=') << "\n" << std::endl;

    // ── 1. 初始化连接池 ──────────────────────────────────────────
    std::cout << "[1] 初始化 MySQL 连接池..." << std::endl;
    MysqlPool pool(kDbHost, kDbPort, kDbUser, kDbPassword, kDbName, 4);
    std::cout << "  可用连接数: " << pool.AvailableCount() << std::endl;

    // ── 2. 建表 ──────────────────────────────────────────────────
    std::cout << "\n[2] 创建 user 表..." << std::endl;
    CreateTable(&pool);

    // ── 3. 初始化 DAO 和 Service ─────────────────────────────────
    UserDAO dao(&pool);
    UserService service(&dao);

    // ── 清理旧数据 ───────────────────────────────────────────────
    Cleanup(&pool);

    int passed = 0;
    int failed = 0;

    // ═══════════════════════════════════════════════════════════════
    // 测试 1: 注册新用户
    // ═══════════════════════════════════════════════════════════════
    std::cout << "\n=== 测试 1: 注册新用户 ===" << std::endl;
    {
        std::string token = service.Register("test_alice", "pass123");
        if (!token.empty()) {
            std::cout << "  [通过] 注册成功, token=" << token.substr(0, 16) << "..." << std::endl;
            passed++;
        } else {
            std::cout << "  [失败] 注册失败" << std::endl;
            failed++;
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 测试 2: 重复注册（应失败）
    // ═══════════════════════════════════════════════════════════════
    std::cout << "\n=== 测试 2: 重复注册（应失败）===" << std::endl;
    {
        std::string token = service.Register("test_alice", "other_pass");
        if (token.empty()) {
            std::cout << "  [通过] 正确拒绝重复用户名" << std::endl;
            passed++;
        } else {
            std::cout << "  [失败] 应该拒绝重复注册" << std::endl;
            failed++;
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 测试 3: 注册第二个用户
    // ═══════════════════════════════════════════════════════════════
    std::cout << "\n=== 测试 3: 注册第二个用户 ===" << std::endl;
    std::string bob_token;
    {
        bob_token = service.Register("test_bob", "secret456");
        if (!bob_token.empty()) {
            std::cout << "  [通过] 第二个用户注册成功" << std::endl;
            passed++;
        } else {
            std::cout << "  [失败]" << std::endl;
            failed++;
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 测试 4: 登录 — 正确密码
    // ═══════════════════════════════════════════════════════════════
    std::cout << "\n=== 测试 4: 登录（正确密码）===" << std::endl;
    {
        std::string token = service.Login("test_alice", "pass123");
        if (!token.empty()) {
            std::cout << "  [通过] 登录成功" << std::endl;
            passed++;
        } else {
            std::cout << "  [失败] 登录失败" << std::endl;
            failed++;
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 测试 5: 登录 — 错误密码
    // ═══════════════════════════════════════════════════════════════
    std::cout << "\n=== 测试 5: 登录（错误密码）===" << std::endl;
    {
        std::string token = service.Login("test_alice", "wrong_password");
        if (token.empty()) {
            std::cout << "  [通过] 正确拒绝错误密码" << std::endl;
            passed++;
        } else {
            std::cout << "  [失败] 应该拒绝错误密码" << std::endl;
            failed++;
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 测试 6: 验证 token（有效 token）
    // ═══════════════════════════════════════════════════════════════
    std::cout << "\n=== 测试 6: 验证 token（有效）===" << std::endl;
    {
        int64_t user_id = service.VerifyToken(bob_token);
        if (user_id > 0) {
            auto user = service.GetUserById(user_id);
            if (user && user->username == "test_bob") {
                std::cout << "  [通过] token 有效, user_id=" << user_id
                          << ", username=" << user->username << std::endl;
                passed++;
            } else {
                std::cout << "  [失败] 用户信息不匹配" << std::endl;
                failed++;
            }
        } else {
            std::cout << "  [失败] token 验证失败" << std::endl;
            failed++;
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 测试 7: 验证 token（无效 token）
    // ═══════════════════════════════════════════════════════════════
    std::cout << "\n=== 测试 7: 验证 token（无效）===" << std::endl;
    {
        int64_t user_id = service.VerifyToken("invalid_token_xxx");
        if (user_id == -1) {
            std::cout << "  [通过] 正确拒绝无效 token" << std::endl;
            passed++;
        } else {
            std::cout << "  [失败] 应该拒绝无效 token" << std::endl;
            failed++;
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 测试 8: RAII 连接归还
    // ═══════════════════════════════════════════════════════════════
    std::cout << "\n=== 测试 8: RAII 连接归还 ===" << std::endl;
    {
        size_t before = pool.AvailableCount();
        {
            auto conn = pool.GetConnection();
            size_t during = pool.AvailableCount();
            std::cout << "  借出前: " << before << ", 借出中: " << during << std::endl;
            assert(during == before - 1 || during < before);
        }  // conn 析构，归还到池
        size_t after = pool.AvailableCount();
        std::cout << "  归还后: " << after << std::endl;
        if (after == before) {
            std::cout << "  [通过] RAII 自动归还成功" << std::endl;
            passed++;
        } else {
            std::cout << "  [失败] 连接未归还" << std::endl;
            failed++;
        }
    }

    // ── 清理 ─────────────────────────────────────────────────────
    Cleanup(&pool);

    // ── 总结 ─────────────────────────────────────────────────────
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "  测试结果: " << passed << " 通过, " << failed << " 失败" << std::endl;
    if (failed == 0) {
        std::cout << "  全部测试通过!" << std::endl;
    }
    std::cout << std::string(60, '=') << "\n" << std::endl;

    return failed > 0 ? 1 : 0;
}
