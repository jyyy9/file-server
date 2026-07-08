#include "gateway/network/tcp_server_wrapper.h"
#include "common/logger.h"

namespace fileserver {
namespace gateway {
namespace network {

TcpServerWrapper::TcpServerWrapper(muduo::net::EventLoop* loop,
                                   const muduo::net::InetAddress& listen_addr,
                                   const std::string& name,
                                   int num_threads)
    : server_(loop, listen_addr, name)
{
    // ── 设置 Sub Reactor 线程数 ──────────────────────────────────
    // num_threads=0: 单 Reactor 模式（所有事件由主线程处理）
    // num_threads>0: 多 Reactor 模式（主线程只 accept，IO 由子线程处理）
    server_.setThreadNum(num_threads);

    // ── 注册连接回调 ─────────────────────────────────────────────
    server_.setConnectionCallback(
        std::bind(&TcpServerWrapper::OnConnection, this, std::placeholders::_1));

    // ── 注册消息回调 ─────────────────────────────────────────────
    server_.setMessageCallback(
        std::bind(&TcpServerWrapper::OnMessage, this,
                  std::placeholders::_1,
                  std::placeholders::_2,
                  std::placeholders::_3));

    LOG_INFO("TcpServerWrapper 创建完成: name=" + name
             + ", listen=" + listen_addr.toIpPort()
             + ", sub_reactors=" + std::to_string(num_threads));
}

TcpServerWrapper::~TcpServerWrapper() {
    if (started_) {
        Stop();
    }
}

void TcpServerWrapper::Start() {
    if (started_) return;
    server_.start();
    started_ = true;
    LOG_INFO("TcpServerWrapper 已启动，开始监听");
}

void TcpServerWrapper::Stop() {
    // muduo TcpServer 没有 stop() 方法
    // 停止服务通过 EventLoop::quit() 实现，server 随 EventLoop 停止而释放
    if (!started_) return;
    started_ = false;
    LOG_INFO("TcpServerWrapper 已停止");
}

void TcpServerWrapper::SetMessageCallback(const MessageCallback& cb) {
    user_message_cb_ = cb;
}

void TcpServerWrapper::SetConnectionCallback(const ConnectionCallback& cb) {
    user_conn_cb_ = cb;
}

// ── 内部: 连接回调 ───────────────────────────────────────────────
// 由 muduo 在 Sub Reactor 的 IO 线程中调用
void TcpServerWrapper::OnConnection(const TcpConnectionPtr& conn) {
    if (conn->connected()) {
        // ── 新连接建立 ───────────────────────────────────────────
        conn_mgr_.AddSession(conn);

        // 调用用户回调
        if (user_conn_cb_) {
            user_conn_cb_(conn);
        }
    } else {
        // ── 连接断开 ─────────────────────────────────────────────
        conn_mgr_.RemoveSession(conn);

        // 调用用户回调
        if (user_conn_cb_) {
            user_conn_cb_(conn);
        }
    }
}

// ── 内部: 消息回调 ───────────────────────────────────────────────
// 由 muduo 在 Sub Reactor 的 IO 线程中调用
void TcpServerWrapper::OnMessage(const TcpConnectionPtr& conn,
                                  Buffer* buf,
                                  Timestamp time) {
    if (user_message_cb_) {
        // 有用户自定义回调 → 交给用户处理
        user_message_cb_(conn, buf, time);
    } else {
        // 默认行为: echo 回显（将收到的数据原样发回）
        conn->send(buf);
    }
}

}  // namespace network
}  // namespace gateway
}  // namespace fileserver
