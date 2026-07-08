#include "services/file_service.h"
#include "common/logger.h"

namespace fileserver {
namespace services {

FileService::FileService(database::FileDAO* file_dao,
                         storage::StorageManager* storage)
    : file_dao_(file_dao)
    , storage_(storage)
{
    LOG_INFO("FileService 初始化完成");
}

// ═══════════════════════════════════════════════════════════════════
// 上传初始化
// ═══════════════════════════════════════════════════════════════════
int64_t FileService::UploadStart(int64_t user_id, const std::string& filename,
                                   int64_t filesize, const std::string& md5) {
    if (filename.empty() || filesize <= 0) {
        LOG_WARNING("UploadStart 参数错误: filename=" + filename
                    + ", filesize=" + std::to_string(filesize));
        return -1;
    }

    // 构建 filepath (存储路径)
    std::string filepath = storage_->BasePath() + "/file_%d.dat";

    database::FileInfo file;
    file.user_id  = user_id;
    file.filename = filename;
    file.filepath = filepath;
    file.filesize = filesize;
    file.md5      = md5;
    file.status   = 0;  // 上传中

    int64_t file_id = file_dao_->Insert(file);
    if (file_id <= 0) {
        LOG_ERROR("UploadStart: 数据库插入失败");
        return -2;
    }

    LOG_INFO("UploadStart: file_id=" + std::to_string(file_id)
             + ", filename=" + filename
             + ", filesize=" + std::to_string(filesize));

    return file_id;
}

// ═══════════════════════════════════════════════════════════════════
// 上传数据块
// ═══════════════════════════════════════════════════════════════════
int64_t FileService::UploadData(int64_t file_id, int64_t user_id,
                                  const std::string& data, int64_t offset) {
    // 1. 验证文件存在且属于该用户
    auto info = file_dao_->FindById(file_id);
    if (!info) {
        LOG_ERROR("UploadData: 文件不存在 file_id=" + std::to_string(file_id));
        return -1;
    }
    if (info->user_id != user_id) {
        LOG_ERROR("UploadData: 用户不匹配");
        return -1;
    }
    if (!info->IsUploading()) {
        LOG_ERROR("UploadData: 文件状态不允许写入 (status="
                  + std::to_string(info->status) + ")");
        return -1;
    }

    // 2. 写入磁盘
    int64_t written = storage_->WriteChunk(file_id, data.data(),
                                            data.size(), offset);
    if (written < 0) {
        return -1;
    }

    // 3. 更新 DB 上传进度
    int64_t new_upload_size = std::max(info->upload_size, offset + written);
    file_dao_->UpdateUploadSize(file_id, new_upload_size);

    return written;
}

// ═══════════════════════════════════════════════════════════════════
// 完成上传
// ═══════════════════════════════════════════════════════════════════
bool FileService::UploadFinalize(int64_t file_id, int64_t user_id) {
    auto info = file_dao_->FindById(file_id);
    if (!info) return false;
    if (info->user_id != user_id) return false;

    // 检查磁盘文件大小是否达到预期
    int64_t disk_size = storage_->GetFileSize(file_id);
    if (disk_size < info->filesize) {
        LOG_WARNING("UploadFinalize: 文件未完整上传, disk="
                    + std::to_string(disk_size)
                    + ", expected=" + std::to_string(info->filesize));
        return false;
    }

    // 标记为完成
    file_dao_->UpdateStatus(file_id, 1);
    file_dao_->UpdateUploadSize(file_id, info->filesize);

    LOG_INFO("UploadFinalize: file_id=" + std::to_string(file_id)
             + " 上传完成, size=" + std::to_string(disk_size));

    return true;
}

// ═══════════════════════════════════════════════════════════════════
// 下载 chunk
// ═══════════════════════════════════════════════════════════════════
std::string FileService::DownloadChunk(int64_t file_id, int64_t user_id,
                                         int64_t offset, size_t size) {
    auto info = file_dao_->FindById(file_id);
    if (!info) return "";
    if (info->user_id != user_id) return "";
    if (info->IsDeleted()) return "";

    return storage_->ReadChunk(file_id, offset, size);
}

// ═══════════════════════════════════════════════════════════════════
// 查询文件列表
// ═══════════════════════════════════════════════════════════════════
std::vector<database::FileInfoPtr> FileService::QueryFiles(int64_t user_id) {
    return file_dao_->FindByUserId(user_id);
}

// ═══════════════════════════════════════════════════════════════════
// 获取文件信息
// ═══════════════════════════════════════════════════════════════════
database::FileInfoPtr FileService::GetFileInfo(int64_t file_id) {
    return file_dao_->FindById(file_id);
}

// ═══════════════════════════════════════════════════════════════════
// 删除文件
// ═══════════════════════════════════════════════════════════════════
bool FileService::Delete(int64_t file_id, int64_t user_id) {
    auto info = file_dao_->FindById(file_id);
    if (!info) return false;
    if (info->user_id != user_id) return false;

    // 1. 软删除 DB 记录
    file_dao_->SoftDelete(file_id);

    // 2. 删除磁盘文件
    storage_->DeleteFile(file_id);

    LOG_INFO("文件已删除: file_id=" + std::to_string(file_id)
             + ", filename=" + info->filename);

    return true;
}

}  // namespace services
}  // namespace fileserver
