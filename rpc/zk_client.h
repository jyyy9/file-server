#ifndef FILESERVER_RPC_ZK_CLIENT_H_
#define FILESERVER_RPC_ZK_CLIENT_H_

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <mutex>
#include <zookeeper/zookeeper.h>

#include "common/common.h"

namespace fileserver {
namespace rpc {

// ── ZooKeeper 客户端 ─────────────────────────────────────────────
//
// 封装 ZooKeeper C API，提供服务注册与发现功能。
//
// 节点结构:
//   /FileServer/{ServiceName}/node_{seq}  → "ip:port"
//
// 使用方式:
//   ZkClient zk("127.0.0.1:2181");
//   zk.Connect();
//   zk.Register("/FileServer/UserService", "127.0.0.1", 9001);
//   auto nodes = zk.Discover("/FileServer/UserService");
//   // nodes = ["127.0.0.1:9001", "127.0.0.1:9002", ...]
class ZkClient {
public:
    explicit ZkClient(const std::string& zk_hosts);
    ~ZkClient();

    FILESERVER_DISALLOW_COPY_AND_MOVE(ZkClient);

    // ── 连接 ─────────────────────────────────────────────────────
    bool Connect(int timeout_ms = 10000);
    bool IsConnected() const { return connected_; }

    // ── 服务注册 ─────────────────────────────────────────────────
    // 在 path 下创建一个临时顺序节点，内容为 "ip:port"
    // 返回创建的完整路径（如 "/FileServer/UserService/node_0000000001"）
    std::string Register(const std::string& path,
                          const std::string& ip, int port);
    // 重载: 直接传 "ip:port"
    std::string Register(const std::string& path, const std::string& addr);

    // ── 服务发现 ─────────────────────────────────────────────────
    // 获取 path 下所有子节点的数据，返回 "ip:port" 列表
    std::vector<std::string> Discover(const std::string& path);

    // ── 确保路径存在 ─────────────────────────────────────────────
    void EnsurePath(const std::string& path);

private:
    static void WatcherCallback(zhandle_t* zh, int type, int state,
                                 const char* path, void* watcherCtx);

    std::string zk_hosts_;
    zhandle_t* handle_{nullptr};
    bool connected_{false};
};

}  // namespace rpc
}  // namespace fileserver

#endif  // FILESERVER_RPC_ZK_CLIENT_H_
