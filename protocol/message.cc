#include "protocol/message.h"

namespace fileserver {
namespace protocol {

nlohmann::json Message::GetJson() const {
    if (json_body.empty()) {
        return nlohmann::json::object();
    }
    return nlohmann::json::parse(json_body, nullptr, false);
}

void Message::SetJson(const nlohmann::json& j) {
    json_body = j.dump();
    header.body_length = static_cast<uint32_t>(json_body.size());
}

std::string Message::GetCmd() const {
    auto j = GetJson();
    if (j.contains("cmd") && j["cmd"].is_string()) {
        return j["cmd"].get<std::string>();
    }
    return "";
}

}  // namespace protocol
}  // namespace fileserver
