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

// ── 文件服务 ─────────────────────────────────────────────────────
//
// 业务逻辑层: 协调 FileDAO (元数据) 和 StorageManager (磁盘IO)
//
// 上传流程:
//   1. UploadStart     — 创建 DB 记录, 返回 file_id (status=0 上传中)
//   2. UploadData      — 写入 chunk 到磁盘, 更新 upload_size
//   3. UploadFinalize  — 验证完整性, 标记 status=1 (完成)
//
// 下载流程:
//   DownloadChunk(file_id, offset, size) — 从磁盘读取 chunk
//
// 删除:
//   Delete → 软删除 DB 记录 (status=2) + 删除磁盘文件
class FileService {
public:
    FileService(database::FileDAO* file_dao, storage::StorageManager* storage);
    ~FileService() = default;

    FILESERVER_DISALLOW_COPY_AND_MOVE(FileService);

    // ── 上传 ─────────────────────────────────────────────────────

    // 初始化上传: 创建 DB 记录, 返回 file_id
    // 返回 -1: 参数错误, -2: 数据库错误
    int64_t UploadStart(int64_t user_id, const std::string& filename,
                         int64_t filesize, const std::string& md5);

    // 上传数据块: 写入磁盘 + 更新 DB 进度
    // 返回实际写入字节数, -1 表示失败
    int64_t UploadData(int64_t file_id, int64_t user_id,
                        const std::string& data, int64_t offset);

    // 完成上传: 验证完整性, 标记为完成
    // 验证 upload_size >= filesize 且文件存在
    bool UploadFinalize(int64_t file_id, int64_t user_id);

    // ── 下载 ─────────────────────────────────────────────────────
    // 按 chunk 读取文件数据 (4MB chunk)
    // 返回读取的数据, 空字符串表示失败或 EOF
    std::string DownloadChunk(int64_t file_id, int64_t user_id,
                               int64_t offset, size_t size);

    // ── 查询 ─────────────────────────────────────────────────────
    // 查询用户的所有文件
    std::vector<database::FileInfoPtr> QueryFiles(int64_t user_id);

    // 获取文件信息
    database::FileInfoPtr GetFileInfo(int64_t file_id);

    // ── 删除 ─────────────────────────────────────────────────────
    bool Delete(int64_t file_id, int64_t user_id);

private:
    database::FileDAO*      file_dao_;   // 不持有所有权
    storage::StorageManager* storage_;    // 不持有所有权
};

}  // namespace services
}  // namespace fileserver

#endif  // FILESERVER_SERVICES_FILE_SERVICE_H_
