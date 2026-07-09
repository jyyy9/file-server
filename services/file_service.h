#ifndef FILESERVER_SERVICES_FILE_SERVICE_H_
#define FILESERVER_SERVICES_FILE_SERVICE_H_

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

#include "database/file_info.h"
#include "database/file_dao.h"
#include "storage/storage_manager.h"
#include "common/common.h"

namespace fileserver {
namespace services {

// ── 上传初始化返回结果 ───────────────────────────────────────────
struct UploadStartResult {
    int64_t file_id   = 0;   // 文件ID (>0 成功, -1 参数错误, -2 DB错误)
    int64_t offset    = 0;   // 已上传字节数（断点续传偏移量）
    bool    is_resume = false; // true=续传, false=新文件
};

// ── 文件服务 ─────────────────────────────────────────────────────
//
// 业务逻辑层: 协调 FileDAO (元数据) 和 StorageManager (磁盘IO)
//
// 上传流程（含断点续传）:
//   客户端                        服务端
//   upload_start ──────────────→  查 DB: 同 user+filename+md5+size 的未完成任务?
//     {filename,filesize,md5}       ├ 有 → 返回 file_id + upload_size (offset)
//                                   └ 无 → 创建新记录, 返回 file_id + offset=0
//
//   seek(offset)  ←──────────── 客户端从 offset 处继续读
//
//   upload_data ───────────────→  seekp(offset) + write(chunk)
//     {file_id,offset,data}       更新 upload_size
//
//   upload_finalize ───────────→  验证 → status=1
//
// 存储路径规则:
//   {storage_root}/{user_id}/{YYYYMMDD}_{HHmmss}_{sanitized_filename}
//   DB filepath = "{user_id}/{YYYYMMDD}_{HHmmss}_{sanitized_filename}"
class FileService {
public:
    FileService(database::FileDAO* file_dao, storage::StorageManager* storage);
    ~FileService() = default;

    FILESERVER_DISALLOW_COPY_AND_MOVE(FileService);

    // ── 上传初始化（支持断点续传）────────────────────────────────
    //
    // 服务端自动检测: 如果存在相同 user_id + filename + filesize + md5
    // 且 status=0 (未完成) 的记录，则返回已有 file_id + upload_size 作为续传偏移量。
    //
    // 参数:
    //   user_id:  用户ID
    //   filename: 原始文件名（纯文件名，客户端不传路径）
    //   filesize: 文件总大小
    //   md5:      文件完整 MD5
    //
    // 返回 UploadStartResult:
    //   .file_id   >0 成功, <=0 失败
    //   .offset    已上传字节数（客户端应从此处继续上传）
    //   .is_resume true 表示续传, false 表示新上传
    UploadStartResult UploadStart(int64_t user_id, const std::string& filename,
                                   int64_t filesize, const std::string& md5);

    // 上传数据块: 在 offset 处写入, 更新 DB 进度
    int64_t UploadData(int64_t file_id, int64_t user_id,
                        const std::string& data, int64_t offset);

    // 完成上传: 验证完整性, 标记 status=1
    bool UploadFinalize(int64_t file_id, int64_t user_id);

    // ── 下载 ─────────────────────────────────────────────────────
    std::string DownloadChunk(int64_t file_id, int64_t user_id,
                               int64_t offset, size_t size);

    // ── 查询 ─────────────────────────────────────────────────────
    std::vector<database::FileInfoPtr> QueryFiles(int64_t user_id);
    database::FileInfoPtr GetFileInfo(int64_t file_id);

    // ── 删除 ─────────────────────────────────────────────────────
    bool Delete(int64_t file_id, int64_t user_id);

private:
    static std::string SanitizeFilename(const std::string& filename);
    static std::string GenerateFilepath(int64_t user_id,
                                         const std::string& filename);

    database::FileDAO*       file_dao_;
    storage::StorageManager* storage_;
};

}  // namespace services
}  // namespace fileserver

#endif  // FILESERVER_SERVICES_FILE_SERVICE_H_
