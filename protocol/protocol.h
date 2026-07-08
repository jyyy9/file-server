#ifndef FILESERVER_PROTOCOL_PROTOCOL_H_
#define FILESERVER_PROTOCOL_PROTOCOL_H_

// ── 协议模块 ─────────────────────────────────────────────────────
// 基于二进制头 + JSON Body + Binary Data 的自定义应用层协议
//
// 协议格式:
//   +----------------------+
//   | MessageHeader (24B)  |  魔数 + 版本 + 类型 + 长度 + 校验
//   +----------------------+
//   | JSON Body            |  变长, JSON 格式
//   +----------------------+
//   | Binary Data          |  变长, 二进制文件数据
//   +----------------------+
//
// 核心类:
//   - MessageHeader  – 24字节定长消息头（魔数、长度、CRC32）
//   - Message        – 完整消息（header + json_body + binary_data）
//   - Encoder        – 序列化 Message → 网络字节流
//   - Decoder        – 反序列化（状态机，处理粘包/半包）

#include "protocol/message_header.h"
#include "protocol/message.h"
#include "protocol/encoder.h"
#include "protocol/decoder.h"

#endif  // FILESERVER_PROTOCOL_PROTOCOL_H_
