#ifndef FILESERVER_GATEWAY_GATEWAY_H_
#define FILESERVER_GATEWAY_GATEWAY_H_

// ── 网关模块（预留）──────────────────────────────────────────────
//
// 网关是系统的接入层服务，负责：
//   - 管理客户端TCP连接（建立、关闭、心跳检测）
//   - 解析JSON应用层协议
//   - 将请求路由到后端RPC服务
//   - 维护每个连接的会话状态
//
// 待实现的核心类：
//   - GatewayServer  – 基于muduo TcpServer的主服务类
//   - ConnectionMgr  – 连接管理器，跟踪活跃连接及其会话
//   - RequestRouter  – 根据 "cmd" 字段分发到对应处理函数

namespace fileserver {
namespace gateway {

// 预留，后续阶段实现

}  // namespace gateway
}  // namespace fileserver

#endif  // FILESERVER_GATEWAY_GATEWAY_H_