#include "gateway/handler/business_processor.h"
#include "common/logger.h"

namespace fileserver {
namespace gateway {
namespace handler {

// ── 构造 ─────────────────────────────────────────────────────────
BusinessProcessor::BusinessProcessor(ThreadPool* pool)
    : pool_(pool)
{
    LOG_INFO("BusinessProcessor 创建完成");
}

// ── 设置任务处理器 ───────────────────────────────────────────────
void BusinessProcessor::SetTaskHandler(const TaskHandler& handler) {
    task_handler_ = handler;
}

// ── 获取消息回调 ─────────────────────────────────────────────────
std::function<void(const TcpConnectionPtr&, Buffer*, Timestamp)>
BusinessProcessor::GetMessageCallback() {
    return std::bind(&BusinessProcessor::OnMessage, this,
                     std::placeholders::_1,
                     std::placeholders::_2,
                     std::placeholders::_3);
}

// ── 获取连接对应的 Decoder ──────────────────────────────────────
std::shared_ptr<protocol::Decoder> BusinessProcessor::GetDecoder(
    const TcpConnectionPtr& conn) {
    const void* key = conn.get();

    std::lock_guard<std::mutex> lock(decoder_mutex_);
    auto it = decoders_.find(key);
    if (it != decoders_.end()) {
        return it->second;
    }

    // 不存在则创建新的 Decoder
    auto decoder = std::make_shared<protocol::Decoder>();
    decoders_[key] = decoder;
    return decoder;
}

void BusinessProcessor::RemoveDecoder(const TcpConnectionPtr& conn) {
    const void* key = conn.get();
    std::lock_guard<std::mutex> lock(decoder_mutex_);
    decoders_.erase(key);
}

size_t BusinessProcessor::ActiveDecoderCount() const {
    std::lock_guard<std::mutex> lock(decoder_mutex_);
    return decoders_.size();
}

// ═══════════════════════════════════════════════════════════════════
// IO 线程回调: 收包 → 解码 → 创建任务 → 投递（不阻塞）
// ═══════════════════════════════════════════════════════════════════
void BusinessProcessor::OnMessage(const TcpConnectionPtr& conn,
                                   Buffer* buf,
                                   Timestamp time) {
    // ── 1. 获取该连接的 Decoder ──────────────────────────────────
    auto decoder = GetDecoder(conn);

    // ── 2. 喂入数据，解析完整消息 ─────────────────────────────────
    // 从 muduo Buffer 中取出可读数据
    std::string raw_data = buf->retrieveAllAsString();

    std::vector<protocol::Message> messages;
    try {
        messages = decoder->Feed(raw_data);
    } catch (const std::exception& e) {
        LOG_ERROR("协议解析异常: " + std::string(e.what()));
        return;
    }

    // ── 3. 每条完整消息 → 创建任务 → 投递线程池 ──────────────────
    for (auto& msg : messages) {
        auto task = std::make_shared<NetworkTask>(msg, conn);

        // 投递到 ThreadPool（非阻塞，立刻返回）
        pool_->Submit([this, task]() {
            this->ProcessTask(task);
        });
    }
}

// ═══════════════════════════════════════════════════════════════════
// Worker 线程回调: 执行业务处理
// ═══════════════════════════════════════════════════════════════════
void BusinessProcessor::ProcessTask(NetworkTaskPtr task) {
    // 计算队列延迟
    int64_t delay_ms = task->ElapsedMs();

    LOG_DEBUG("Worker 处理任务: cmd=" + task->request.GetCmd()
              + ", request_id=" + std::to_string(task->request.header.request_id)
              + ", queue_delay=" + std::to_string(delay_ms) + "ms");

    if (task_handler_) {
        // 有自定义业务处理 → 交给用户
        task_handler_(*task, delay_ms);
    } else {
        // ── 默认处理: 收到什么回什么（echo JSON body）─────────────
        // 注意: conn->send() 线程安全，Worker 线程可直接调用
        if (task->conn && task->conn->connected()) {
            std::string response_body =
                "{\"code\":0,\"msg\":\"ok\",\"echo_cmd\":\""
                + task->request.GetCmd() + "\"}";

            // 用 Encoder 构建响应
            protocol::Message response;
            response.header.msg_type   = static_cast<uint32_t>(protocol::MessageType::kResponse);
            response.header.request_id = task->request.header.request_id;
            response.SetJson(nlohmann::json::parse(response_body));

            std::string wire = protocol::Encoder::Encode(response);

            // conn->send() 是线程安全的，内部 queueInLoop
            task->conn->send(wire);
        }
    }
}

}  // namespace handler
}  // namespace gateway
}  // namespace fileserver
