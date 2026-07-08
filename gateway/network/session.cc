#include "gateway/network/session.h"

namespace fileserver {
namespace gateway {
namespace network {

Session::Session(uint64_t id, const TcpConnectionPtr& conn)
    : id_(id)
    , conn_(conn)                         // shared_ptr → weak_ptr 隐式转换
    , peer_addr_(conn->peerAddress().toIpPort())
{
    // 获取当前时间（毫秒时间戳）
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()).count();
    create_time_ = ms;
    last_active_time_ = ms;
}

Session::~Session() {
    // Session 析构时不需要通知连接，由 muduo 管理连接的释放
}

void Session::UpdateLastActiveTime() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()).count();
    last_active_time_ = ms;
}

Session::TcpConnectionPtr Session::GetConnection() const {
    return conn_.lock();   // weak_ptr::lock() → shared_ptr (可能为空)
}

}  // namespace network
}  // namespace gateway
}  // namespace fileserver
