#ifndef FILESERVER_RPC_RPC_SERVER_H_
#define FILESERVER_RPC_RPC_SERVER_H_

#include <string>
#include <functional>
#include <unordered_map>
#include <memory>

#include <muduo/net/TcpServer.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <nlohmann/json.hpp>

#include "protocol/message.h"
#include "protocol/encoder.h"
#include "protocol/decoder.h"
#include "threadpool/thread_pool.h"
#include "common/common.h"

namespace fileserver {
namespace rpc {

// ── RPC 方法处理器 ───────────────────────────────────────────────
// 接收 JSON 参数，返回 JSON 结果
// 在 Worker 线程中执行（可进行 DB/磁盘 IO）
using RpcMethod = std::function<nlohmann::json(const nlohmann::json& args)>;

// ── RPC 服务器 ───────────────────────────────────────────────────
//
// 基于 muduo TcpServer + 自定义二进制协议，提供 RPC 服务。
//
// 使用方式:
//   RpcServer server(&loop, InetAddress(9001), "UserService");
//   server.RegisterMethod("Login", [](auto& args) { ... });
//   server.RegisterMethod("Register", [](auto& args) { ... });
//   server.Start();
//
// 内部架构:
//   Sub Reactor: 收包 → 解码 Message
//   ThreadPool Worker: 查找 method → 执行 → 编码响应
//   Sub Reactor: 发送响应
class RpcServer {
public:
    RpcServer(muduo::net::EventLoop* loop,
              const muduo::net::InetAddress& addr,
              const std::string& service_name,
              int num_threads = 4,
              int worker_threads = 4);
    ~RpcServer();

    FILESERVER_DISALLOW_COPY_AND_MOVE(RpcServer);

    // ── 方法注册 ─────────────────────────────────────────────────
    void RegisterMethod(const std::string& name, const RpcMethod& method);

    // ── 启动 ─────────────────────────────────────────────────────
    void Start();

    // ── 服务名 ───────────────────────────────────────────────────
    const std::string& ServiceName() const { return service_name_; }

private:
    void OnConnection(const muduo::net::TcpConnectionPtr& conn);
    void OnMessage(const muduo::net::TcpConnectionPtr& conn,
                   muduo::net::Buffer* buf, muduo::Timestamp);

    std::string service_name_;
    muduo::net::TcpServer server_;
    ThreadPool worker_pool_;
    std::unordered_map<std::string, RpcMethod> methods_;
    std::unordered_map<const void*, std::shared_ptr<protocol::Decoder>> decoders_;
    std::mutex decoder_mutex_;
};

}  // namespace rpc
}  // namespace fileserver

#endif  // FILESERVER_RPC_RPC_SERVER_H_
