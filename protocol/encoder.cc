#include "protocol/encoder.h"
#include <cstring>

namespace fileserver {
namespace protocol {

// ── CRC32 查表（标准多项式 0xEDB88320）─────────────────────────
uint32_t Encoder::crc32_table_[256] = {0};
bool     Encoder::crc32_initialized_ = false;

void Encoder::InitCRC32Table() {
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (int j = 0; j < 8; ++j) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
        crc32_table_[i] = crc;
    }
    crc32_initialized_ = true;
}

uint32_t Encoder::CRC32(const void* data, size_t len) {
    if (!crc32_initialized_) {
        InitCRC32Table();
    }
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; ++i) {
        crc = crc32_table_[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

// ── 编码单条消息 ─────────────────────────────────────────────────
std::string Encoder::Encode(const Message& msg) {
    // 1. 构建 header（先填长度，checksum 暂置 0）
    MessageHeader h = msg.header;
    h.body_length = static_cast<uint32_t>(msg.json_body.size());
    h.data_length = static_cast<uint32_t>(msg.binary_data.size());
    h.checksum = 0;

    // 2. 序列化 header → 24 字节网络字节序
    char header_buf[MessageHeader::kHeaderSize];
    h.EncodeTo(header_buf);

    // 3. 拼接完整数据: header + body + data（用于 CRC 计算）
    std::string payload;
    payload.reserve(MessageHeader::kHeaderSize + msg.json_body.size() + msg.binary_data.size());
    payload.append(header_buf, MessageHeader::kHeaderSize);
    payload.append(msg.json_body);
    payload.append(msg.binary_data);

    // 4. 计算 CRC32 并回填到 header
    uint32_t crc = CRC32(payload.data(), payload.size());
    h.checksum = crc;

    // 5. 重新序列化 header（带正确的 checksum）
    h.EncodeTo(header_buf);

    // 6. 构建最终输出
    std::string result;
    result.reserve(payload.size());
    result.append(header_buf, MessageHeader::kHeaderSize);
    result.append(msg.json_body);
    result.append(msg.binary_data);

    return result;
}

// ── 批量编码 ─────────────────────────────────────────────────────
std::string Encoder::EncodeBatch(const std::vector<Message>& msgs) {
    std::string result;
    for (const auto& msg : msgs) {
        result += Encode(msg);
    }
    return result;
}

}  // namespace protocol
}  // namespace fileserver
