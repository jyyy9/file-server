#ifndef FILESERVER_DATABASE_FILE_INFO_H_
#define FILESERVER_DATABASE_FILE_INFO_H_

#include <cstdint>
#include <string>
#include <memory>

namespace fileserver {
namespace database {

// ── 文件状态 ─────────────────────────────────────────────────────
// FileStatus 定义在 common/common.h 的 fileserver 命名空间中

// ── 文件元数据实体 ───────────────────────────────────────────────
struct FileInfo {
    int64_t  id          = 0;
    int64_t  user_id     = 0;
    std::string filename;
    std::string filepath;
    int64_t  filesize    = 0;    // 文件总大小 (字节)
    int64_t  upload_size = 0;    // 已上传大小 (字节), 用于断点续传
    std::string md5;
    int32_t  status      = 0;    // 0=上传中, 1=完成, 2=删除
    std::string create_time;

    bool IsUploading() const { return status == 0; }
    bool IsCompleted() const { return status == 1; }
    bool IsDeleted()   const { return status == 2; }
};

using FileInfoPtr = std::shared_ptr<FileInfo>;

}  // namespace database
}  // namespace fileserver

#endif  // FILESERVER_DATABASE_FILE_INFO_H_
