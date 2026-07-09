#include "gateway/handler/request_router.h"
#include "protocol/encoder.h"
#include "common/logger.h"

namespace fileserver {
namespace gateway {
namespace handler {

void RequestRouter::Register(const std::string& cmd, const MsgHandler& handler) {
    handlers_[cmd] = handler;
    LOG_INFO("路由注册: cmd=" + cmd);
}

protocol::Message RequestRouter::Route(const protocol::Message& request) {
    std::string cmd = request.GetCmd();

    auto it = handlers_.find(cmd);
    if (it == handlers_.end()) {
        // 未注册的 cmd → 返回错误
        LOG_WARNING("未知命令: " + cmd);
        protocol::Message resp;
        resp.header.msg_type   = static_cast<uint32_t>(protocol::MessageType::kResponse);
        resp.header.request_id = request.header.request_id;

        nlohmann::json err;
        err["code"] = -3;
        err["msg"] = "未知命令: " + cmd;
        resp.SetJson(err);
        return resp;
    }

    // 路由到 handler
    return it->second(request);
}

}  // namespace handler
}  // namespace gateway
}  // namespace fileserver
