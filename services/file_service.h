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
// 存储路径规则:
//   {storage_root}/{user_id}/{YYYYMMDD}_{HHmmss}_{original_filename}
//
//   DB filepath 存储相对路径: {user_id}/{YYYYMMDD}_{HHmmss}_{original_filename}
//   根路径由 StorageManager 拼接，迁移时只改配置不改数据库
//
// 安全约束:
//   - 客户端只传 filename（纯文件名），不传路径
//   - 服务端生成实际存储路径
//   - filename 做 sanitize: 去除 ../ \ 等危险字符
class FileService {
public:
    FileService(database::FileDAO* file_dao, storage::StorageManager* storage);
    ~FileService() = default;

    FILESERVER_DISALLOW_COPY_AND_MOVE(FileService);

    // ── 上传 ─────────────────────────────────────────────────────

    // 初始化上传: 创建 DB 记录, 返回 file_id
    // user_id:  用户ID（决定存储子目录）
    // filename: 客户端传来的原始文件名（纯文件名，不含路径）
    // filesize: 文件总大小（字节）
    // md5:      文件 MD5（用于完整性校验）
    // 返回: file_id (>0), -1 参数错误, -2 数据库错误
    int64_t UploadStart(int64_t user_id, const std::string& filename,
                         int64_t filesize, const std::string& md5);

    // 上传数据块: 写入磁盘 + 更新 DB 进度
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
    // ── 文件名安全处理 ──────────────────────────────────────────
    // 去除路径分隔符、上级目录等危险字符
    static std::string SanitizeFilename(const std::string& filename);

    // ── 生成存储相对路径 ────────────────────────────────────────
    // 格式: {user_id}/{YYYYMMDD}_{HHmmss}_{sanitized_filename}
    static std::string GenerateFilepath(int64_t user_id,
                                         const std::string& filename);

    database::FileDAO*       file_dao_;
    storage::StorageManager* storage_;
};

}  // namespace services
}  // namespace fileserver

#endif  // FILESERVER_SERVICES_FILE_SERVICE_H_
