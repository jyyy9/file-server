#ifndef FILESERVER_GATEWAY_HANDLER_TASK_H_
#define FILESERVER_GATEWAY_HANDLER_TASK_H_

#include <cstdint>
#include <chrono>
#include <memory>

#include <muduo/net/TcpConnection.h>

#include "protocol/message.h"

namespace fileserver {
namespace gateway {
namespace handler {

// ── 网络业务任务 ─────────────────────────────────────────────────
//
// 由 IO 线程创建，投递到 ThreadPool，由 Worker 线程执行。
//
// 生命周期:
//   1. IO 线程收到数据 → Decoder 解析 → 创建 NetworkTask
//   2. pool->Submit() 投递到任务队列
//   3. Worker 线程取出 → 执行 handler → conn->send() 发送响应
//   4. conn (shared_ptr) 保证连接在任务执行期间不会被释放
struct NetworkTask {
    using TcpConnectionPtr = muduo::net::TcpConnectionPtr;

    // 已解码的协议消息
    protocol::Message request;

    // 连接指针 (shared_ptr，防止任务执行期间连接被释放)
    TcpConnectionPtr conn;

    // 入队时间戳（毫秒）
    int64_t enqueue_time_ms;

    // ── 构造 ─────────────────────────────────────────────────────
    NetworkTask()
        : enqueue_time_ms(0) {}

    NetworkTask(const protocol::Message& req, const TcpConnectionPtr& c)
        : request(req)
        , conn(c)
    {
        auto now = std::chrono::system_clock::now();
        enqueue_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              now.time_since_epoch()).count();
    }

    // ── 延迟计算（从入队到现在的毫秒数）─────────────────────────
    int64_t ElapsedMs() const {
        auto now = std::chrono::system_clock::now();
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          now.time_since_epoch()).count();
        return now_ms - enqueue_time_ms;
    }
};

using NetworkTaskPtr = std::shared_ptr<NetworkTask>;

}  // namespace handler
}  // namespace gateway
}  // namespace fileserver

#endif  // FILESERVER_GATEWAY_HANDLER_TASK_H_
