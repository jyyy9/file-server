#ifndef FILESERVER_GATEWAY_NETWORK_SESSION_H_
#define FILESERVER_GATEWAY_NETWORK_SESSION_H_

#include <cstdint>
#include <memory>
#include <string>
#include <chrono>

#include <muduo/net/TcpConnection.h>

#include "common/common.h"

namespace fileserver {
namespace gateway {
namespace network {

// ── 会话类 ───────────────────────────────────────────────────────
//
// 表示一个客户端连接的会话状态。
// 由 TcpConnectionManager 创建和管理生命周期。
//
// 设计要点:
//   - 使用 weak_ptr 持有 TcpConnection，避免循环引用
//   - 记录连接时间和活跃时间，用于后续心跳/超时检测
//   - Session 本身由 shared_ptr 管理，ConnectionManager 持有所有权
class Session {
public:
    using TcpConnectionPtr   = muduo::net::TcpConnectionPtr;
    using TcpConnectionWeakPtr = std::weak_ptr<muduo::net::TcpConnection>;

    // 构造函数
    // id:   全局唯一的会话ID，由 ConnectionManager 分配
    // conn: muduo TcpConnection 的 shared_ptr
    Session(uint64_t id, const TcpConnectionPtr& conn);

    ~Session();

    // ── 属性访问 ─────────────────────────────────────────────────
    uint64_t Id() const { return id_; }
    const TcpConnectionWeakPtr& Connection() const { return conn_; }
    const std::string& PeerAddress() const { return peer_addr_; }
    int64_t CreateTime() const { return create_time_; }
    int64_t LastActiveTime() const { return last_active_time_; }

    // 更新最后活跃时间（收到消息时调用）
    void UpdateLastActiveTime();

    // 获取对端的 TcpConnectionPtr（若连接已释放则返回空）
    TcpConnectionPtr GetConnection() const;

private:
    uint64_t id_;                   // 会话ID
    TcpConnectionWeakPtr conn_;     // 弱引用，避免循环引用
    std::string peer_addr_;         // 对端地址 (IP:Port)
    int64_t create_time_;           // 创建时间 (ms since epoch)
    int64_t last_active_time_;      // 最后活跃时间 (ms since epoch)
};

using SessionPtr = std::shared_ptr<Session>;

}  // namespace network
}  // namespace gateway
}  // namespace fileserver

#endif  // FILESERVER_GATEWAY_NETWORK_SESSION_H_
