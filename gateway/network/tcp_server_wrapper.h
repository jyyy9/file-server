#ifndef FILESERVER_GATEWAY_NETWORK_TCP_SERVER_WRAPPER_H_
#define FILESERVER_GATEWAY_NETWORK_TCP_SERVER_WRAPPER_H_

#include <functional>
#include <string>
#include <memory>

#include <muduo/net/TcpServer.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/TcpConnection.h>
#include <muduo/net/Buffer.h>
#include <muduo/base/Timestamp.h>

#include "gateway/network/tcp_connection_manager.h"
#include "common/common.h"

namespace fileserver {
namespace gateway {
namespace network {

// ── TcpServer 封装 ───────────────────────────────────────────────
//
// 封装 muduo::net::TcpServer，内部管理:
//   - 主从 Reactor 线程模型（TcpServer 内置 EventLoopThreadPool）
//   - TcpConnectionManager（会话生命周期）
//   - 默认的 echo 消息处理和连接日志
//
// 使用方法:
//   1. 创建 EventLoop（主 Reactor）
//   2. 创建 TcpServerWrapper，设置监听地址和 Sub Reactor 线程数
//   3. （可选）通过 SetMessageCallback 注入自定义消息处理
//   4. Start() 启动服务
//   5. 调用 loop.loop() 进入事件循环
//
// IO线程安全约束:
//   - OnConnection / OnMessage 回调在 Sub Reactor 的 IO 线程中执行
//   - 用户自定义回调中 不允许执行耗时操作（DB查询、文件IO等）
//   - 耗时操作应提交到 ThreadPool 异步执行
class TcpServerWrapper {
public:
    using TcpConnectionPtr = muduo::net::TcpConnectionPtr;
    using Buffer           = muduo::net::Buffer;
    using Timestamp        = muduo::Timestamp;

    // 消息回调签名: (连接指针, 接收缓冲区, 时间戳)
    using MessageCallback = std::function<void(
        const TcpConnectionPtr& conn,
        Buffer* buf,
        Timestamp time)>;

    // 连接回调签名: (连接指针)
    using ConnectionCallback = std::function<void(
        const TcpConnectionPtr& conn)>;

    // 构造函数
    // loop:         主 Reactor EventLoop（负责 accept）
    // listen_addr:  监听地址 (如 InetAddress(8080))
    // name:         服务名称（用于日志标识）
    // num_threads:  Sub Reactor 线程数，0 表示单 Reactor 模式
    TcpServerWrapper(muduo::net::EventLoop* loop,
                     const muduo::net::InetAddress& listen_addr,
                     const std::string& name,
                     int num_threads);

    ~TcpServerWrapper();

    FILESERVER_DISALLOW_COPY_AND_MOVE(TcpServerWrapper);

    // 启动服务（开始监听）
    void Start();

    // 停止服务（关闭所有连接，停止监听）
    void Stop();

    // 设置自定义消息回调，覆盖默认的 echo 行为
    void SetMessageCallback(const MessageCallback& cb);

    // 设置自定义连接回调，在默认日志之后调用
    void SetConnectionCallback(const ConnectionCallback& cb);

    // 获取连接管理器（只读访问）
    TcpConnectionManager& GetConnectionManager() { return conn_mgr_; }

    // 当前活跃连接数
    size_t ConnectionCount() const { return conn_mgr_.ConnectionCount(); }

private:
    // ── 内部回调（由 muduo 在 IO 线程中调用）────────────────────
    void OnConnection(const TcpConnectionPtr& conn);
    void OnMessage(const TcpConnectionPtr& conn,
                   Buffer* buf,
                   Timestamp time);

    muduo::net::TcpServer server_;           // muduo TcpServer
    TcpConnectionManager conn_mgr_;           // 连接管理器
    MessageCallback user_message_cb_;         // 用户自定义消息回调
    ConnectionCallback user_conn_cb_;         // 用户自定义连接回调
    bool started_{false};                     // 是否已启动
};

}  // namespace network
}  // namespace gateway
}  // namespace fileserver

#endif  // FILESERVER_GATEWAY_NETWORK_TCP_SERVER_WRAPPER_H_
