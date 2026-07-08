#ifndef FILESERVER_STORAGE_STORAGE_MANAGER_H_
#define FILESERVER_STORAGE_STORAGE_MANAGER_H_

#include <string>
#include <cstdint>

#include "common/common.h"

namespace fileserver {
namespace storage {

// ── 存储管理器 ───────────────────────────────────────────────────
//
// 负责文件的磁盘读写。所有操作按 chunk 进行，禁止一次性加载整个
// 文件到内存。
//
// 文件存储路径: base_path/file_{file_id}.dat
//
// 关键约束:
//   - WriteChunk: 支持任意 offset 写入（断点续传场景）
//   - ReadChunk:  按 offset + size 读取，只加载需要的 chunk 到内存
//   - 4MB chunk 是默认大小，但接口支持任意大小
class StorageManager {
public:
    // base_path: 文件存储根目录
    explicit StorageManager(const std::string& base_path);
    ~StorageManager() = default;

    FILESERVER_DISALLOW_COPY_AND_MOVE(StorageManager);

    // ── 写入 ─────────────────────────────────────────────────────
    // 在 offset 位置写入 data 数据
    // 返回实际写入字节数，失败返回 -1
    int64_t WriteChunk(int64_t file_id, const char* data,
                       size_t len, int64_t offset);

    // ── 读取 ─────────────────────────────────────────────────────
    // 从 offset 位置读取 size 字节
    // 返回读取的数据，实际大小可能小于请求（文件尾部）
    std::string ReadChunk(int64_t file_id, int64_t offset, size_t size);

    // ── 删除 ─────────────────────────────────────────────────────
    bool DeleteFile(int64_t file_id);

    // ── 查询 ─────────────────────────────────────────────────────
    // 获取文件在磁盘上的实际大小
    int64_t GetFileSize(int64_t file_id);

    // 获取文件路径
    std::string GetFilePath(int64_t file_id) const;

    // 存储根目录
    const std::string& BasePath() const { return base_path_; }

private:
    std::string base_path_;
};

}  // namespace storage
}  // namespace fileserver

#endif  // FILESERVER_STORAGE_STORAGE_MANAGER_H_
