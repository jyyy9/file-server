#ifndef FILESERVER_STORAGE_STORAGE_H_
#define FILESERVER_STORAGE_STORAGE_H_

// ── 存储模块（预留）──────────────────────────────────────────────
//
// 本模块负责文件的实际磁盘读写：
//   - 按偏移量分块写入文件
//   - 按偏移量分块读取文件
//   - 合并分块为完整文件
//   - 管理文件存储路径布局
//
// 待实现的核心类：
//   - FileManager    – 文件创建/打开/删除，管理存储目录结构
//   - ChunkManager   – 按偏移量读写分块，支持断点续传

namespace fileserver {
namespace storage {

// 预留，后续阶段实现

}  // namespace storage
}  // namespace fileserver

#endif  // FILESERVER_STORAGE_STORAGE_H_