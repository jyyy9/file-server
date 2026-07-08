#ifndef FILESERVER_PROTOCOL_MESSAGE_H_
#define FILESERVER_PROTOCOL_MESSAGE_H_

#include <string>
#include <nlohmann/json.hpp>

#include "protocol/message_header.h"
#include "common/common.h"

namespace fileserver {
namespace protocol {

// ── 协议消息 ─────────────────────────────────────────────────────
//
// 一条完整的协议消息，包含:
//   - header:    消息头（魔数、长度、校验等元信息）
//   - json_body: JSON 格式的消息体（可读文本）
//   - binary_data: 可选二进制数据（文件内容等）
//
// 使用场景:
//   - 请求: JSON body 含 cmd/参数, binary_data 空
//   - 上传: JSON body 含上传元信息, binary_data 含文件块
class Message {
public:
    MessageHeader header;
    std::string json_body;
    std::string binary_data;

    // ── 构造 ─────────────────────────────────────────────────────
    Message() = default;
    ~Message() = default;

    // ── JSON 快捷方法 ────────────────────────────────────────────
    // 将 json_body 解析为 nlohmann::json 对象
    nlohmann::json GetJson() const;

    // 将 nlohmann::json 对象序列化到 json_body
    void SetJson(const nlohmann::json& j);

    // 快捷获取 cmd 字段（如 "login", "upload_start" 等）
    std::string GetCmd() const;

    // ── 消息体总长度 ─────────────────────────────────────────────
    size_t BodyLength() const { return json_body.size(); }
    size_t DataLength() const { return binary_data.size(); }

    // ── 校验 ─────────────────────────────────────────────────────
    bool IsValid() const {
        return header.magic == kProtocolMagic;
    }
};

}  // namespace protocol
}  // namespace fileserver

#endif  // FILESERVER_PROTOCOL_MESSAGE_H_
