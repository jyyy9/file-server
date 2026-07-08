#include "gateway/network/tcp_connection_manager.h"
#include "common/logger.h"

namespace fileserver {
namespace gateway {
namespace network {

SessionPtr TcpConnectionManager::AddSession(const TcpConnectionPtr& conn) {
    uint64_t id = next_id_.fetch_add(1, std::memory_order_relaxed);

    auto session = std::make_shared<Session>(id, conn);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_[id] = session;
    }

    LOG_INFO("新连接建立: session_id=" + std::to_string(id)
             + ", peer=" + conn->peerAddress().toIpPort()
             + ", 当前连接数=" + std::to_string(sessions_.size()));

    return session;
}

void TcpConnectionManager::RemoveSession(uint64_t session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        sessions_.erase(it);
        LOG_INFO("连接断开: session_id=" + std::to_string(session_id)
                 + ", 当前连接数=" + std::to_string(sessions_.size()));
    }
}

void TcpConnectionManager::RemoveSession(const TcpConnectionPtr& conn) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 遍历查找匹配的连接指针
    for (auto it = sessions_.begin(); it != sessions_.end(); ++it) {
        auto session_conn = it->second->GetConnection();
        if (session_conn.get() == conn.get()) {
            uint64_t id = it->first;
            sessions_.erase(it);
            LOG_INFO("连接断开: session_id=" + std::to_string(id)
                     + ", peer=" + conn->peerAddress().toIpPort()
                     + ", 当前连接数=" + std::to_string(sessions_.size()));
            return;
        }
    }
}

SessionPtr TcpConnectionManager::GetSession(uint64_t session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<SessionPtr> TcpConnectionManager::GetAllSessions() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SessionPtr> result;
    result.reserve(sessions_.size());
    for (const auto& pair : sessions_) {
        result.push_back(pair.second);
    }
    return result;
}

size_t TcpConnectionManager::ConnectionCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.size();
}

}  // namespace network
}  // namespace gateway
}  // namespace fileserver
