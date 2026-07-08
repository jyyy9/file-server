#ifndef FILESERVER_PROTOCOL_ENCODER_H_
#define FILESERVER_PROTOCOL_ENCODER_H_

#include <string>
#include <cstdint>
#include <vector>

#include "protocol/message.h"
#include "protocol/message_header.h"

namespace fileserver {
namespace protocol {

// ── 编码器 ───────────────────────────────────────────────────────
//
// 将 Message 序列化为网络传输格式:
//
//   offset 0:   MessageHeader (24B, 网络字节序, 含 CRC32 校验)
//   offset 24:  JSON Body      (body_length 字节)
//   offset 24+: Binary Data    (data_length 字节)
//
// CRC32 范围: header(checksum=0) + body + data
class Encoder {
public:
    // 将单条 Message 编码为网络字节流
    static std::string Encode(const Message& msg);

    // 批量编码多条 Message（首尾拼接，用于粘包测试）
    static std::string EncodeBatch(const std::vector<Message>& msgs);

    // CRC32 公开方法（供 Decoder 验证使用）
    static uint32_t CRC32(const void* data, size_t len);

private:
    static void InitCRC32Table();

    static uint32_t crc32_table_[256];
    static bool     crc32_initialized_;
};

}  // namespace protocol
}  // namespace fileserver

#endif  // FILESERVER_PROTOCOL_ENCODER_H_
