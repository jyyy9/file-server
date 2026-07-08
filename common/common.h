#ifndef FILESERVER_COMMON_COMMON_H_
#define FILESERVER_COMMON_COMMON_H_

#include <cstdint>
#include <string>
#include <memory>
#include <vector>
#include <map>

namespace fileserver {

// ── 基础类型定义 ─────────────────────────────────────────────────
using StringList  = std::vector<std::string>;
using StringMap   = std::map<std::string, std::string>;

// ── 错误码枚举 ───────────────────────────────────────────────────
enum class ErrorCode : int32_t {
    kOk          = 0,    // 成功
    kUnknown     = -1,   // 未知错误
    kParamError  = -2,   // 参数错误
    kAuthFailed  = -3,   // 认证失败
    kNotFound    = -4,   // 资源未找到
    kIOError     = -5,   // IO错误
    kDBError     = -6,   // 数据库错误
    kRPCError    = -7,   // RPC调用错误
    kTimeout     = -8,   // 超时
    kFileExists  = -9,   // 文件已存在
    kDiskFull    = -10,  // 磁盘已满
};

// ── 文件状态枚举 ─────────────────────────────────────────────────
enum class FileStatus : int32_t {
    kUploading  = 0,   // 上传中
    kCompleted  = 1,   // 上传完成
    kDeleted    = 2,   // 已删除
};

// ── 工具宏 ───────────────────────────────────────────────────────
#define FILESERVER_DISALLOW_COPY(ClassName)     \
    ClassName(const ClassName&) = delete;       \
    ClassName& operator=(const ClassName&) = delete

#define FILESERVER_DISALLOW_MOVE(ClassName)     \
    ClassName(ClassName&&) = delete;            \
    ClassName& operator=(ClassName&&) = delete

#define FILESERVER_DISALLOW_COPY_AND_MOVE(ClassName) \
    FILESERVER_DISALLOW_COPY(ClassName);             \
    FILESERVER_DISALLOW_MOVE(ClassName)

// ── 常量定义 ─────────────────────────────────────────────────────
constexpr int32_t kDefaultPort         = 8080;                  // 默认监听端口
constexpr int32_t kMaxConnectionCount  = 65536;                 // 最大连接数
constexpr int32_t kBufferSize          = 4096;                  // 默认缓冲区大小
constexpr int32_t kDefaultChunkSize    = 4 * 1024 * 1024;       // 默认分块大小 4MB，太小了请求太多开销大，太大了内存吃不消，4MB 是个折中值。
constexpr const char* kDefaultLogFile  = "fileserver.log";      // 默认日志文件

}  // namespace fileserver

#endif  // FILESERVER_COMMON_COMMON_H_