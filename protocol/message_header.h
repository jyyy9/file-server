#ifndef FILESERVER_PROTOCOL_MESSAGE_HEADER_H_
#define FILESERVER_PROTOCOL_MESSAGE_HEADER_H_

#include <cstdint>
#include <cstring>
#include <string>
#include <arpa/inet.h>

namespace fileserver {
namespace protocol {

// ── 魔数定义 ─────────────────────────────────────────────────────
constexpr uint16_t kProtocolMagic   = 0x4D46;   // "MF" (MessageFormat)
constexpr uint16_t kProtocolVersion = 0x0001;   // 协议版本 1

// ── 消息类型 ─────────────────────────────────────────────────────
enum class MessageType : uint32_t {
    kRequest   = 0x01,   // 请求消息
    kResponse  = 0x02,   // 响应消息
    kNotify    = 0x03,   // 通知消息（单向，无需响应）
};

// ── 消息头 (24字节, 紧凑布局) ────────────────────────────────────
//
// 网络传输时所有多字节字段使用网络字节序（大端）。
// 布局:
//   offset 0:  magic       (2B)
//   offset 2:  version     (2B)
//   offset 4:  msg_type    (4B)
//   offset 8:  body_length (4B)  -- JSON Body 长度
//   offset 12: data_length (4B)  -- Binary Data 长度
//   offset 16: request_id  (4B)  -- 请求ID
//   offset 20: checksum    (4B)  -- CRC32 校验
// 总计: 24 字节
#pragma pack(push, 1)
struct MessageHeader {
    uint16_t magic;         // 协议魔数 0x4D46
    uint16_t version;       // 协议版本
    uint32_t msg_type;      // 消息类型
    uint32_t body_length;   // JSON Body 长度（字节）
    uint32_t data_length;   // Binary Data 长度（字节）
    uint32_t request_id;    // 请求ID
    uint32_t checksum;      // CRC32 校验

    // ── 默认构造 ─────────────────────────────────────────────────
    MessageHeader()
        : magic(kProtocolMagic)
        , version(kProtocolVersion)
        , msg_type(0)
        , body_length(0)
        , data_length(0)
        , request_id(0)
        , checksum(0)
    {}

    // ── 编码: 主机字节序 → 网络字节序（大端）────────────────────
    // 将 header 字段转换为网络字节序写入 buffer（24 字节）
    void EncodeTo(char* buffer) const {
        uint16_t net_magic   = htons(magic);
        uint16_t net_version = htons(version);
        uint32_t net_type    = htonl(msg_type);
        uint32_t net_body    = htonl(body_length);
        uint32_t net_data    = htonl(data_length);
        uint32_t net_reqid   = htonl(request_id);
        uint32_t net_crc     = htonl(checksum);

        std::memcpy(buffer,      &net_magic,   2);
        std::memcpy(buffer + 2,  &net_version, 2);
        std::memcpy(buffer + 4,  &net_type,    4);
        std::memcpy(buffer + 8,  &net_body,    4);
        std::memcpy(buffer + 12, &net_data,    4);
        std::memcpy(buffer + 16, &net_reqid,   4);
        std::memcpy(buffer + 20, &net_crc,     4);
    }

    // ── 解码: 网络字节序（大端）→ 主机字节序 ────────────────────
    // 从 buffer 中读取 24 字节并还原为 header 字段
    // 返回 false 表示魔数不匹配
    bool DecodeFrom(const char* buffer) {
        uint16_t net_magic, net_version;
        uint32_t net_type, net_body, net_data, net_reqid, net_crc;

        std::memcpy(&net_magic,    buffer,      2);
        std::memcpy(&net_version,  buffer + 2,  2);
        std::memcpy(&net_type,     buffer + 4,  4);
        std::memcpy(&net_body,     buffer + 8,  4);
        std::memcpy(&net_data,     buffer + 12, 4);
        std::memcpy(&net_reqid,    buffer + 16, 4);
        std::memcpy(&net_crc,      buffer + 20, 4);

        magic       = ntohs(net_magic);
        version     = ntohs(net_version);
        msg_type    = ntohl(net_type);
        body_length = ntohl(net_body);
        data_length = ntohl(net_data);
        request_id  = ntohl(net_reqid);
        checksum    = ntohl(net_crc);

        return magic == kProtocolMagic;
    }

    // ── 常量 ─────────────────────────────────────────────────────
    static constexpr size_t kHeaderSize = 24;
};
#pragma pack(pop)

// 编译期验证头大小
static_assert(sizeof(MessageHeader) == 24, "MessageHeader must be 24 bytes");

}  // namespace protocol
}  // namespace fileserver

#endif  // FILESERVER_PROTOCOL_MESSAGE_HEADER_H_
