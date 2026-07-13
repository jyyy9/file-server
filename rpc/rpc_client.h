#ifndef FILESERVER_RPC_RPC_CLIENT_H_
#define FILESERVER_RPC_RPC_CLIENT_H_

#include <string>
#include <future>
#include <mutex>
#include <unordered_map>
#include <atomic>
#include <memory>

#include <muduo/net/TcpClient.h>
#include <muduo/net/EventLoopThread.h>
#include <nlohmann/json.hpp>

#include "protocol/encoder.h"
#include "protocol/decoder.h"
#include "common/common.h"

namespace fileserver {
namespace rpc {

// ── RPC 客户端 ───────────────────────────────────────────────────
//
// 连接远程 RpcServer，发起同步 RPC 调用。
//
// 使用方式:
//   RpcClient client;
//   client.Connect("127.0.0.1", 9001);
//   auto result = client.Call("Login", {{"username","admin"},{"password","123456"}});
//   // result = {"code":0, "token":"xxx"}
class RpcClient {
public:
    using TcpConnectionPtr = muduo::net::TcpConnectionPtr;

    RpcClient();
    ~RpcClient();

    FILESERVER_DISALLOW_COPY_AND_MOVE(RpcClient);

    // ── 连接 ─────────────────────────────────────────────────────
    bool Connect(const std::string& host, int port);
    bool IsConnected() const { return connected_; }
    void Disconnect();

    // ── 调用 ─────────────────────────────────────────────────────
    // 同步 RPC 调用，阻塞等待响应
    nlohmann::json Call(const std::string& method,
                         const nlohmann::json& args,
                         int timeout_ms = 5000);

private:
    void OnConnection(const TcpConnectionPtr& conn);
    void OnMessage(const TcpConnectionPtr& conn,
                   muduo::net::Buffer* buf, muduo::Timestamp);

    uint32_t NextId();
    void SetPending(uint32_t id, std::promise<nlohmann::json>* p);
    void ResolvePending(uint32_t id, const nlohmann::json& result);

    muduo::net::EventLoopThread loop_thread_;
    muduo::net::EventLoop* loop_;
    std::unique_ptr<muduo::net::TcpClient> tcp_client_;
    TcpConnectionPtr connection_;
    protocol::Decoder decoder_;
    std::atomic<bool> connected_{false};

    std::mutex pending_mutex_;
    std::unordered_map<uint32_t, std::promise<nlohmann::json>*> pending_;
    std::atomic<uint32_t> next_id_{1};
};

}  // namespace rpc
}  // namespace fileserver

#endif  // FILESERVER_RPC_RPC_CLIENT_H_
