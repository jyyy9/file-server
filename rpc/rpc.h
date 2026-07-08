#ifndef FILESERVER_RPC_RPC_H_
#define FILESERVER_RPC_RPC_H_

// ── RPC模块（预留）───────────────────────────────────────────────
//
// 本模块提供服务间RPC通信框架（mprpc）：
//   - 通过ZooKeeper实现服务注册
//   - 服务发现与负载均衡
//   - RPC调用的序列化与反序列化
//   - 多服务实例间的负载均衡
//
// 待实现的核心类：
//   - RpcServer      – 注册本地服务，监听远程调用
//   - RpcClient      – 发现并调用远程服务
//   - ZkClient       – ZooKeeper客户端，管理服务注册信息

namespace fileserver {
namespace rpc {

// 预留，后续阶段实现

}  // namespace rpc
}  // namespace fileserver

#endif  // FILESERVER_RPC_RPC_H_