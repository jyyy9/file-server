#ifndef FILESERVER_PROTOCOL_DECODER_H_
#define FILESERVER_PROTOCOL_DECODER_H_

#include <string>
#include <vector>
#include <cstddef>

#include "protocol/message.h"
#include "protocol/message_header.h"

namespace fileserver {
namespace protocol {

// ── 解码器 ───────────────────────────────────────────────────────
//
// 从 TCP 字节流中解析完整 Message，解决粘包/半包问题。
//
// 工作方式 — 状态机:
//
//   kWaitHeader ──→ 累积 ≥ 24B → 解析 header → 校验 magic
//        │              │
//        │         magic OK → 获取 bodyLen + dataLen
//        │              │
//        │         buffer ≥ header+body+data?
//        │           ├── 是 → 提取完整消息 → 返回
//        │           └── 否 → 切换到 kWaitBody（等待更多数据）
//        │
//   kWaitBody ──→ 累积 ≥ bodyLen+dataLen
//        │
//        提取 JSON body + binary data
//        │
//        校验 CRC32
//        │
//        产出 Message → 切回 kWaitHeader
//        │
//        剩余数据继续解析（处理粘包）
//
// 使用方式:
//   Decoder decoder;
//   auto msgs = decoder.Feed(data, len);   // 每次收到数据就喂进去
//   // msgs 是本次解析出的完整 Message 列表
class Decoder {
public:
    Decoder() = default;

    // ── 喂入数据，返回解析出的完整消息列表 ──────────────────────
    // data/len:  新收到的 TCP 数据
    // 返回:      本次解析出的所有完整 Message
    //            如果数据不完整（半包），返回空列表
    //            如果包含多条消息（粘包），返回多条
    std::vector<Message> Feed(const char* data, size_t len);
    std::vector<Message> Feed(const std::string& data);

    // 重置解码器状态
    void Reset();

    // 当前内部缓冲区的未解析字节数
    size_t BufferedBytes() const { return buffer_.size(); }

private:
    // ── 状态 ─────────────────────────────────────────────────────
    enum class State {
        kWaitHeader,   // 等待完整 header（24 字节）
        kWaitBody,     // 等待完整 body + data
    };

    // 尝试从 buffer_ 中提取一条完整消息
    // 返回 true 表示成功提取，false 表示数据不足
    bool TryParseMessage(Message& msg);

    State state_{State::kWaitHeader};
    std::string buffer_;                    // 未解析完成的累积数据
    MessageHeader current_header_;          // kWaitBody 状态下已解析的 header
};

}  // namespace protocol
}  // namespace fileserver

#endif  // FILESERVER_PROTOCOL_DECODER_H_
