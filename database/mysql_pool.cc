#include "database/mysql_pool.h"
#include "common/logger.h"

namespace fileserver {
namespace database {

MysqlPool::MysqlPool(const std::string& host, int port,
                     const std::string& user, const std::string& password,
                     const std::string& db, size_t pool_size)
    : host_(host), port_(port)
    , user_(user), password_(password)
    , db_(db), pool_size_(pool_size)
{
    // 预创建连接
    for (size_t i = 0; i < pool_size_; ++i) {
        MYSQL* conn = CreateConnection();
        if (conn) {
            pool_.push(conn);
            total_created_++;
        }
    }

    LOG_INFO("MysqlPool 初始化完成: " + host_ + ":" + std::to_string(port_)
             + "/" + db_ + ", 连接数=" + std::to_string(pool_.size()));
}

MysqlPool::~MysqlPool() {
    // 关闭所有连接
    std::lock_guard<std::mutex> lock(mutex_);
    while (!pool_.empty()) {
        MYSQL* conn = pool_.front();
        pool_.pop();
        mysql_close(conn);
    }
    LOG_INFO("MysqlPool 已销毁，释放 " + std::to_string(total_created_) + " 个连接");
}

MYSQL* MysqlPool::CreateConnection() {
    MYSQL* conn = mysql_init(nullptr);
    if (!conn) {
        LOG_ERROR("mysql_init 失败");
        return nullptr;
    }

    // 设置自动重连
    my_bool reconnect = 1;
    mysql_options(conn, MYSQL_OPT_RECONNECT, &reconnect);

    if (!mysql_real_connect(conn, host_.c_str(), user_.c_str(),
                            password_.c_str(), db_.c_str(), port_,
                            nullptr, 0)) {
        LOG_ERROR(std::string("MySQL 连接失败: ") + mysql_error(conn));
        mysql_close(conn);
        return nullptr;
    }

    // 设置 UTF-8 编码
    mysql_set_character_set(conn, "utf8mb4");

    return conn;
}

std::shared_ptr<MYSQL> MysqlPool::GetConnection() {
    std::unique_lock<std::mutex> lock(mutex_);

    // 等待可用连接
    cond_.wait(lock, [this] {
        return !pool_.empty() || total_created_ < pool_size_;
    });

    MYSQL* conn = nullptr;

    if (!pool_.empty()) {
        conn = pool_.front();
        pool_.pop();
    } else if (total_created_ < pool_size_) {
        // 池中暂时没空闲连接但还可以创建新的
        conn = CreateConnection();
        if (conn) {
            total_created_++;
        }
    }

    if (!conn) {
        return nullptr;
    }

    // 检查连接是否有效，无效则重连
    if (mysql_ping(conn) != 0) {
        LOG_WARNING("MySQL 连接断开，尝试重连...");
        if (!mysql_real_connect(conn, host_.c_str(), user_.c_str(),
                                password_.c_str(), db_.c_str(), port_,
                                nullptr, 0)) {
            LOG_ERROR("MySQL 重连失败");
            mysql_close(conn);
            return nullptr;
        }
    }

    // 使用自定义删除器：析构时归还到池
    return std::shared_ptr<MYSQL>(conn, [this](MYSQL* c) {
        this->ReturnConnection(c);
    });
}

void MysqlPool::ReturnConnection(MYSQL* conn) {
    if (!conn) return;
    std::lock_guard<std::mutex> lock(mutex_);
    pool_.push(conn);
    cond_.notify_one();  // 通知等待的线程
}

size_t MysqlPool::AvailableCount() {
    std::lock_guard<std::mutex> lock(mutex_);
    return pool_.size();
}

}  // namespace database
}  // namespace fileserver
