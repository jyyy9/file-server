#ifndef FILESERVER_GATEWAY_HANDLER_BUSINESS_PROCESSOR_H_
#define FILESERVER_GATEWAY_HANDLER_BUSINESS_PROCESSOR_H_

#include <functional>
#include <memory>
#include <unordered_map>
#include <mutex>

#include <muduo/net/TcpConnection.h>
#include <muduo/net/Buffer.h>
#include <muduo/base/Timestamp.h>

#include "threadpool/thread_pool.h"
#include "protocol/decoder.h"
#include "gateway/handler/task.h"
#include "common/common.h"

namespace fileserver {
namespace gateway {
namespace handler {

// ── 业务处理器 ───────────────────────────────────────────────────
//
// 连接网络层（IO线程）和业务层（Worker线程）的桥梁。
//
// 工作流程:
//   IO 线程: 收包 → Decoder 解析 → 创建 NetworkTask → Submit 到 ThreadPool
//   Worker:  取任务 → 执行业务逻辑 → conn->send(响应)
//
// 关键设计:
//   - 每个连接独立一个 Decoder（处理粘包/半包状态）
//   - IO 线程不做耗时操作，只做解码+投递
//   - conn->send() 线程安全，Worker 可直接调用
//   - 使用 shared_ptr<TcpConnection> 保证任务执行期间连接有效
class BusinessProcessor {
public:
    using TcpConnectionPtr = muduo::net::TcpConnectionPtr;
    using Buffer           = muduo::net::Buffer;
    using Timestamp        = muduo::Timestamp;

    // ── 任务处理回调 ─────────────────────────────────────────────
    // Worker 线程收到任务后调用此回调
    // 参数: (NetworkTask, 入队到执行的延迟ms)
    using TaskHandler = std::function<void(
        NetworkTask& task, int64_t queue_delay_ms)>;

    // ── 构造 ─────────────────────────────────────────────────────
    // pool: 线程池引用（BusinessProcessor 不拥有所有权）
    explicit BusinessProcessor(ThreadPool* pool);

    ~BusinessProcessor() = default;
    FILESERVER_DISALLOW_COPY_AND_MOVE(BusinessProcessor);

    // ── 设置任务处理器 ───────────────────────────────────────────
    // 设置后在 Worker 线程中执行
    // 不设置则使用默认 echo（回显 JSON body）
    void SetTaskHandler(const TaskHandler& handler);

    // ── 获取消息回调（注册到 TcpServerWrapper）───────────────────
    // 返回的 callback 可直接传给 TcpServerWrapper::SetMessageCallback
    std::function<void(const TcpConnectionPtr&, Buffer*, Timestamp)>
    GetMessageCallback();

    // ── 统计 ─────────────────────────────────────────────────────
    size_t ActiveDecoderCount() const;

private:
    // ── IO 线程回调 ──────────────────────────────────────────────
    void OnMessage(const TcpConnectionPtr& conn,
                   Buffer* buf,
                   Timestamp time);

    // ── Worker 线程回调 ──────────────────────────────────────────
    void ProcessTask(NetworkTaskPtr task);

    // ── 获取/创建 连接对应的 Decoder ─────────────────────────────
    std::shared_ptr<protocol::Decoder> GetDecoder(const TcpConnectionPtr& conn);
    void RemoveDecoder(const TcpConnectionPtr& conn);

    ThreadPool* pool_;                                   // 不持有所有权
    TaskHandler task_handler_;                           // 用户自定义任务处理

    // 每个连接一个 Decoder（key = TcpConnection 原始指针）
    mutable std::mutex decoder_mutex_;
    std::unordered_map<const void*, std::shared_ptr<protocol::Decoder>> decoders_;
};

}  // namespace handler
}  // namespace gateway
}  // namespace fileserver

#endif  // FILESERVER_GATEWAY_HANDLER_BUSINESS_PROCESSOR_H_
