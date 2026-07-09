#ifndef FILESERVER_GATEWAY_HANDLER_REQUEST_ROUTER_H_
#define FILESERVER_GATEWAY_HANDLER_REQUEST_ROUTER_H_

#include <string>
#include <functional>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "protocol/message.h"
#include "common/common.h"

namespace fileserver {
namespace gateway {
namespace handler {

// ── 请求路由器 ───────────────────────────────────────────────────
//
// 根据 Message JSON body 中的 "cmd" 字段分发到对应的处理函数。
//
// 处理函数签名:
//   MsgHandler: 接收请求 Message, 返回响应 Message
//   在 Worker 线程中执行（可进行 DB/磁盘 IO）
using MsgHandler = std::function<protocol::Message(
    const protocol::Message& request)>;

class RequestRouter {
public:
    RequestRouter() = default;
    FILESERVER_DISALLOW_COPY_AND_MOVE(RequestRouter);

    // 注册 cmd → handler 映射
    void Register(const std::string& cmd, const MsgHandler& handler);

    // 路由请求到 handler，返回响应
    // 如果 cmd 未注册，返回错误响应
    protocol::Message Route(const protocol::Message& request);

private:
    std::unordered_map<std::string, MsgHandler> handlers_;
};

}  // namespace handler
}  // namespace gateway
}  // namespace fileserver

#endif  // FILESERVER_GATEWAY_HANDLER_REQUEST_ROUTER_H_
