#include "rpc/rpc_server.h"
#include "common/logger.h"

namespace fileserver {
namespace rpc {

RpcServer::RpcServer(muduo::net::EventLoop* loop,
                     const muduo::net::InetAddress& addr,
                     const std::string& service_name,
                     int num_threads, int worker_threads)
    : service_name_(service_name)
    , server_(loop, addr, "RpcServer:" + service_name)
    , worker_pool_(worker_threads)
{
    server_.setThreadNum(num_threads);
    server_.setConnectionCallback(
        std::bind(&RpcServer::OnConnection, this, std::placeholders::_1));
    server_.setMessageCallback(
        std::bind(&RpcServer::OnMessage, this,
                  std::placeholders::_1, std::placeholders::_2,
                  std::placeholders::_3));

    LOG_INFO("RpcServer 创建: " + service_name
             + " on " + addr.toIpPort());
}

RpcServer::~RpcServer() {
    LOG_INFO("RpcServer 销毁: " + service_name_);
}

void RpcServer::RegisterMethod(const std::string& name,
                                 const RpcMethod& method) {
    methods_[name] = method;
    LOG_INFO("RPC方法注册: " + service_name_ + "." + name);
}

void RpcServer::Start() {
    server_.start();
    LOG_INFO("RpcServer 启动: " + service_name_);
}

// ── 连接回调 ─────────────────────────────────────────────────────
void RpcServer::OnConnection(const muduo::net::TcpConnectionPtr& conn) {
    if (conn->connected()) {
        // 每个连接一个新的 Decoder
        std::lock_guard<std::mutex> lock(decoder_mutex_);
        decoders_[conn.get()] = std::make_shared<protocol::Decoder>();
    } else {
        std::lock_guard<std::mutex> lock(decoder_mutex_);
        decoders_.erase(conn.get());
    }
}

// ── 消息回调（IO线程）───────────────────────────────────────────
void RpcServer::OnMessage(const muduo::net::TcpConnectionPtr& conn,
                           muduo::net::Buffer* buf, muduo::Timestamp) {
    // 获取该连接的 Decoder
    std::shared_ptr<protocol::Decoder> decoder;
    {
        std::lock_guard<std::mutex> lock(decoder_mutex_);
        auto it = decoders_.find(conn.get());
        if (it == decoders_.end()) return;
        decoder = it->second;
    }

    std::string raw = buf->retrieveAllAsString();
    auto msgs = decoder->Feed(raw);

    for (auto& msg : msgs) {
        auto j = msg.GetJson();
        std::string method = j.value("method", "");

        // 投递到 Worker 线程池执行
        worker_pool_.Submit([this, conn, method, msg]() {
            nlohmann::json result;
            auto it = methods_.find(method);
            if (it != methods_.end()) {
                try {
                    result = it->second(msg.GetJson());
                } catch (const std::exception& e) {
                    result["code"] = -1;
                    result["msg"]  = std::string("RPC异常: ") + e.what();
                }
            } else {
                result["code"] = -2;
                result["msg"]  = "未知方法: " + method;
            }

            // 构造响应
            protocol::Message resp;
            resp.header.msg_type   = static_cast<uint32_t>(protocol::MessageType::kResponse);
            resp.header.request_id = msg.header.request_id;
            resp.SetJson(result);

            std::string wire = protocol::Encoder::Encode(resp);
            if (conn->connected()) {
                conn->send(wire);
            }
        });
    }
}

}  // namespace rpc
}  // namespace fileserver
