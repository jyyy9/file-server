#include "rpc/rpc_client.h"
#include <muduo/net/EventLoop.h>
#include "common/logger.h"

#include <thread>
#include <chrono>

namespace fileserver {
namespace rpc {

RpcClient::RpcClient()
    : loop_(loop_thread_.startLoop()) {}

RpcClient::~RpcClient() {
    Disconnect();
}

bool RpcClient::Connect(const std::string& host, int port) {
    tcp_client_.reset(new muduo::net::TcpClient(loop_,
        muduo::net::InetAddress(host, port), "RpcClient"));
    tcp_client_->setConnectionCallback(
        std::bind(&RpcClient::OnConnection, this, std::placeholders::_1));
    tcp_client_->setMessageCallback(
        std::bind(&RpcClient::OnMessage, this,
                  std::placeholders::_1, std::placeholders::_2,
                  std::placeholders::_3));
    tcp_client_->connect();

    for (int i = 0; i < 30 && !connected_; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    return connected_;
}

void RpcClient::Disconnect() {
    connected_ = false;
    connection_.reset();
}

void RpcClient::OnConnection(const TcpConnectionPtr& conn) {
    connected_ = conn->connected();
    if (connected_) connection_ = conn;
    else connection_.reset();
}

void RpcClient::OnMessage(const TcpConnectionPtr&,
                           muduo::net::Buffer* buf, muduo::Timestamp) {
    auto msgs = decoder_.Feed(buf->retrieveAllAsString());
    for (auto& msg : msgs) {
        ResolvePending(msg.header.request_id, msg.GetJson());
    }
}

uint32_t RpcClient::NextId() { return next_id_.fetch_add(1); }

void RpcClient::SetPending(uint32_t id, std::promise<nlohmann::json>* p) {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_[id] = p;
}

void RpcClient::ResolvePending(uint32_t id, const nlohmann::json& r) {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    auto it = pending_.find(id);
    if (it != pending_.end()) {
        it->second->set_value(r);
        pending_.erase(it);
    }
}

// ── 同步调用 ─────────────────────────────────────────────────────
nlohmann::json RpcClient::Call(const std::string& method,
                                  const nlohmann::json& args,
                                  int timeout_ms) {
    if (!connection_) return {{"code", -1}, {"msg", "未连接"}};

    protocol::Message msg;
    msg.header.msg_type   = static_cast<uint32_t>(protocol::MessageType::kRequest);
    msg.header.request_id = NextId();

    nlohmann::json j = args;
    j["method"] = method;
    msg.SetJson(j);

    std::string wire = protocol::Encoder::Encode(msg);
    std::promise<nlohmann::json> promise;
    auto future = promise.get_future();
    SetPending(msg.header.request_id, &promise);

    loop_->runInLoop([this, wire]() {
        if (connection_) connection_->send(wire);
    });

    auto status = future.wait_for(std::chrono::milliseconds(timeout_ms));
    if (status == std::future_status::timeout) {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_.erase(msg.header.request_id);
        return {{"code", -3}, {"msg", "RPC超时"}};
    }
    return future.get();
}

}  // namespace rpc
}  // namespace fileserver
