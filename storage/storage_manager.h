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
// 路径规则:
//   绝对路径 = base_path_ / filepath
//   其中 filepath 来自数据库 file_info.filepath 字段
//   格式: {user_id}/{YYYYMMDD}_{HHmmss}_{original_filename}
//
// 示例:
//   base_path_ = "./storage/data"
//   filepath   = "1/20250709_143025_video.mp4"
//   完整路径   = "./storage/data/1/20250709_143025_video.mp4"
//
// 关键约束:
//   - WriteChunk: 支持任意 offset 写入（断点续传场景）
//   - ReadChunk:  按 offset + size 读取，只加载需要的 chunk 到内存
//   - 4MB chunk 是默认大小，但接口支持任意大小
//   - 从不根据 file_id 推断路径，路径由 Service 层从 DB 查得后传入
class StorageManager {
public:
    // base_path: 文件存储根目录（可配置）
    explicit StorageManager(const std::string& base_path);
    ~StorageManager() = default;

    FILESERVER_DISALLOW_COPY_AND_MOVE(StorageManager);

    // ── 目录管理 ─────────────────────────────────────────────────
    // 创建用户上传子目录（如果不存在）
    // user_id: 用户ID, 在 base_path_ 下创建对应目录
    bool CreateUserDir(int64_t user_id);

    // ── 写入 ─────────────────────────────────────────────────────
    // 在 filepath 的 offset 位置写入 data 数据
    // filepath: DB 中存储的相对路径 (如 "1/20250709_143025_video.mp4")
    // 返回实际写入字节数，失败返回 -1
    int64_t WriteChunk(const std::string& filepath, const char* data,
                       size_t len, int64_t offset);

    // ── 读取 ─────────────────────────────────────────────────────
    // 从 filepath 的 offset 位置读取 size 字节
    // 返回读取的数据，实际大小可能小于请求（文件尾部）
    std::string ReadChunk(const std::string& filepath,
                          int64_t offset, size_t size);

    // ── 删除 ─────────────────────────────────────────────────────
    bool DeleteFile(const std::string& filepath);

    // ── 查询 ─────────────────────────────────────────────────────
    // 获取文件在磁盘上的实际大小（字节）
    int64_t GetFileSize(const std::string& filepath);

    // 存储根目录
    const std::string& BasePath() const { return base_path_; }

    // 构建完整绝对路径
    std::string GetAbsolutePath(const std::string& filepath) const;

private:
    std::string base_path_;
};

}  // namespace storage
}  // namespace fileserver

#endif  // FILESERVER_STORAGE_STORAGE_MANAGER_H_
