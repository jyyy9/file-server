#include "protocol/decoder.h"
#include "protocol/encoder.h"
#include "common/logger.h"

#include <cstring>

namespace fileserver {
namespace protocol {

// ── 喂入数据 ─────────────────────────────────────────────────────
std::vector<Message> Decoder::Feed(const char* data, size_t len) {
    // 追加到内部缓冲区
    buffer_.append(data, len);

    std::vector<Message> result;

    // 循环解析：每提取一条完整消息，继续尝试（处理粘包）
    while (true) {
        Message msg;
        if (!TryParseMessage(msg)) {
            break;   // 数据不足，等待下次 Feed
        }
        result.push_back(std::move(msg));
    }

    return result;
}

std::vector<Message> Decoder::Feed(const std::string& data) {
    return Feed(data.data(), data.size());
}

// ── 重置 ─────────────────────────────────────────────────────────
void Decoder::Reset() {
    state_ = State::kWaitHeader;
    buffer_.clear();
}

// ── 尝试解析一条完整消息 ─────────────────────────────────────────
bool Decoder::TryParseMessage(Message& msg) {
    if (state_ == State::kWaitHeader) {
        // ── 等待 header ──────────────────────────────────────────
        if (buffer_.size() < MessageHeader::kHeaderSize) {
            return false;   // 半包：header 还没收全
        }

        // 解析 header
        if (!current_header_.DecodeFrom(buffer_.data())) {
            // 魔数不匹配 → 丢弃 1 字节再试（容错恢复）
            LOG_ERROR("协议魔数不匹配，丢弃1字节");
            buffer_.erase(0, 1);
            return false;
        }

        // magic OK，切换到等待 body 状态
        state_ = State::kWaitBody;

        // 已消费 header 部分，暂时保留在 buffer 中方便后续 CRC
        // 消费时机在 body+data 完整后统一移除
    }

    // ── 等待 body + data ─────────────────────────────────────────
    if (state_ == State::kWaitBody) {
        size_t total_needed = MessageHeader::kHeaderSize
                            + current_header_.body_length
                            + current_header_.data_length;

        if (buffer_.size() < total_needed) {
            return false;   // 半包：body/data 还没收全
        }

        // ── 提取 JSON body ───────────────────────────────────────
        const char* body_start = buffer_.data() + MessageHeader::kHeaderSize;
        msg.json_body.assign(body_start, current_header_.body_length);

        // ── 提取 binary data ─────────────────────────────────────
        const char* data_start = body_start + current_header_.body_length;
        msg.binary_data.assign(data_start, current_header_.data_length);

        // ── CRC32 校验 ───────────────────────────────────────────
        // 重建 header(checksum=0) + body + data 的 CRC 并对比
        MessageHeader h = current_header_;
        h.checksum = 0;

        char header_buf[MessageHeader::kHeaderSize];
        h.EncodeTo(header_buf);

        // 拼接 header_buf + body + data
        std::string payload;
        payload.reserve(MessageHeader::kHeaderSize
                      + current_header_.body_length
                      + current_header_.data_length);
        payload.append(header_buf, MessageHeader::kHeaderSize);
        payload.append(msg.json_body);
        payload.append(msg.binary_data);

        uint32_t calculated = Encoder::CRC32(payload.data(), payload.size());

        if (calculated != current_header_.checksum) {
            LOG_ERROR("CRC32 校验失败: expected=0x"
                      + std::to_string(current_header_.checksum)
                      + ", actual=0x" + std::to_string(calculated));
            // 校验失败 → 丢弃整条消息的头 1 字节再试
            buffer_.erase(0, 1);
            state_ = State::kWaitHeader;
            return false;
        }

        // ── 组装 Message ─────────────────────────────────────────
        msg.header = current_header_;

        // ── 消费已解析数据 ───────────────────────────────────────
        buffer_.erase(0, total_needed);

        // ── 回到等待 header 状态（处理后续消息）──────────────────
        state_ = State::kWaitHeader;

        return true;
    }

    return false;
}

}  // namespace protocol
}  // namespace fileserver
