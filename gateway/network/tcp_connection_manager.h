#ifndef FILESERVER_GATEWAY_NETWORK_TCP_CONNECTION_MANAGER_H_
#define FILESERVER_GATEWAY_NETWORK_TCP_CONNECTION_MANAGER_H_

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <atomic>

#include <muduo/net/TcpConnection.h>

#include "gateway/network/session.h"
#include "common/common.h"

namespace fileserver {
namespace gateway {
namespace network {

// ── TCP连接管理器 ────────────────────────────────────────────────
//
// 管理所有活跃的客户端连接对应的 Session。
// 线程安全 —— 所有公开方法内部加锁，可被多线程同时调用。
//
// 职责:
//   - 新连接到达时创建 Session（分配唯一ID）
//   - 连接断开时删除 Session
//   - 提供按ID查询、遍历全部会话的能力
class TcpConnectionManager {
public:
    using TcpConnectionPtr = muduo::net::TcpConnectionPtr;

    TcpConnectionManager() = default;
    ~TcpConnectionManager() = default;

    FILESERVER_DISALLOW_COPY_AND_MOVE(TcpConnectionManager);

    // 新连接到达 —— 创建并注册一个 Session
    // 返回新创建的 Session 指针
    SessionPtr AddSession(const TcpConnectionPtr& conn);

    // 按会话ID移除
    void RemoveSession(uint64_t session_id);

    // 按连接指针移除（连接断开时常用）
    void RemoveSession(const TcpConnectionPtr& conn);

    // 按ID查询会话，不存在返回 nullptr
    SessionPtr GetSession(uint64_t session_id);

    // 获取全部会话的快照（遍历时不影响其他操作）
    std::vector<SessionPtr> GetAllSessions();

    // 当前活跃连接数
    size_t ConnectionCount() const;

private:
    mutable std::mutex mutex_;                                    // 互斥锁
    std::unordered_map<uint64_t, SessionPtr> sessions_;           // id → Session
    std::atomic<uint64_t> next_id_{1};                            // 自增ID生成器
};

}  // namespace network
}  // namespace gateway
}  // namespace fileserver

#endif  // FILESERVER_GATEWAY_NETWORK_TCP_CONNECTION_MANAGER_H_
