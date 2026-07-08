#ifndef FILESERVER_PROTOCOL_PROTOCOL_H_
#define FILESERVER_PROTOCOL_PROTOCOL_H_

// ── 协议模块（预留）──────────────────────────────────────────────
//
// 本模块实现基于长度前缀的自定义JSON应用层协议：
//
//   +--------------+--------------+--------------+
//   | 长度 (4字节)  | JSON 消息体   | 二进制数据    |
//   +--------------+--------------+--------------+
//
// 职责：
//   - 消息编解码（JSON <-> 字节流）
//   - TCP粘包/半包处理
//   - 命令路由（login, upload_start, upload_data 等）
//
// 待实现的核心类：
//   - JsonCodec     – JSON消息编解码器
//   - Message       – 一条请求/响应的内存表示
//   - LengthHeader  – 4字节大端序长度头

namespace fileserver {
namespace protocol {

// 预留，后续阶段实现

}  // namespace protocol
}  // namespace fileserver

#endif  // FILESERVER_PROTOCOL_PROTOCOL_H_