#include "rpc/zk_client.h"
#include "common/logger.h"

#include <cstring>
#include <thread>
#include <chrono>

namespace fileserver {
namespace rpc {

ZkClient::ZkClient(const std::string& zk_hosts)
    : zk_hosts_(zk_hosts) {}

ZkClient::~ZkClient() {
    if (handle_) {
        zookeeper_close(handle_);
        handle_ = nullptr;
    }
}

// ── 全局 watcher ─────────────────────────────────────────────────
void ZkClient::WatcherCallback(zhandle_t*, int type, int state,
                                const char*, void* ctx) {
    auto* self = static_cast<ZkClient*>(ctx);
    if (state == ZOO_CONNECTED_STATE) {
        self->connected_ = true;
        LOG_INFO("ZooKeeper 已连接");
    } else if (state == ZOO_EXPIRED_SESSION_STATE) {
        self->connected_ = false;
        LOG_WARNING("ZooKeeper 会话过期");
    }
}

// ── 连接 ─────────────────────────────────────────────────────────
bool ZkClient::Connect(int timeout_ms) {
    handle_ = zookeeper_init(zk_hosts_.c_str(), WatcherCallback,
                              timeout_ms, nullptr, this, 0);
    if (!handle_) {
        LOG_ERROR("zookeeper_init 失败");
        return false;
    }

    // 等待连接建立
    int waited = 0;
    while (!connected_ && waited < timeout_ms) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waited += 100;
    }

    if (!connected_) {
        LOG_ERROR("ZooKeeper 连接超时");
        return false;
    }

    LOG_INFO("ZooKeeper 连接成功: " + zk_hosts_);
    return true;
}

// ── 确保路径存在 ─────────────────────────────────────────────────
void ZkClient::EnsurePath(const std::string& path) {
    if (path.empty() || path == "/") return;

    // 逐级创建
    size_t pos = 1;
    while (pos < path.size()) {
        size_t next = path.find('/', pos);
        if (next == std::string::npos) next = path.size();
        std::string sub = path.substr(0, next);

        if (sub.size() > 1) {
            int rc = zoo_create(handle_, sub.c_str(), nullptr, -1,
                                &ZOO_OPEN_ACL_UNSAFE, 0, nullptr, 0);
            if (rc != ZOK && rc != ZNODEEXISTS) {
                LOG_WARNING("创建ZK节点失败: " + sub);
            }
        }

        pos = next + 1;
    }
}

// ── 注册 ─────────────────────────────────────────────────────────
std::string ZkClient::Register(const std::string& path,
                                 const std::string& ip, int port) {
    return Register(path, ip + ":" + std::to_string(port));
}

std::string ZkClient::Register(const std::string& path,
                                 const std::string& addr) {
    if (!connected_ || !handle_) return "";

    // 确保父路径存在
    EnsurePath(path);

    // 创建临时顺序节点
    std::string full_path = path + "/node_";
    char created_path[512] = {0};

    int rc = zoo_create(handle_, full_path.c_str(),
                         addr.c_str(), addr.size(),
                         &ZOO_OPEN_ACL_UNSAFE,
                         ZOO_EPHEMERAL | ZOO_SEQUENCE,
                         created_path, sizeof(created_path));

    if (rc == ZOK) {
        LOG_INFO("ZK注册: " + std::string(created_path) + " → " + addr);
        return created_path;
    }

    LOG_ERROR("ZK注册失败: " + path + " rc=" + std::to_string(rc));
    return "";
}

// ── 服务发现 ─────────────────────────────────────────────────────
std::vector<std::string> ZkClient::Discover(const std::string& path) {
    std::vector<std::string> result;
    if (!connected_ || !handle_) return result;

    struct String_vector children;
    int rc = zoo_get_children(handle_, path.c_str(), 0, &children);
    if (rc != ZOK) {
        LOG_WARNING("ZK发现失败: " + path + " rc=" + std::to_string(rc));
        return result;
    }

    for (int i = 0; i < children.count; ++i) {
        std::string child_path = path + "/" + children.data[i];
        char buffer[256] = {0};
        int buf_len = sizeof(buffer);
        struct Stat stat;

        rc = zoo_get(handle_, child_path.c_str(), 0,
                      buffer, &buf_len, &stat);
        if (rc == ZOK && buf_len > 0) {
            std::string addr(buffer, buf_len);
            result.push_back(addr);
        }
    }

    deallocate_String_vector(&children);
    return result;
}

}  // namespace rpc
}  // namespace fileserver
