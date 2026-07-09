#include "services/file_service.h"
#include "common/logger.h"

#include <ctime>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace fileserver {
namespace services {

FileService::FileService(database::FileDAO* file_dao,
                         storage::StorageManager* storage)
    : file_dao_(file_dao)
    , storage_(storage)
{
    LOG_INFO("FileService 初始化完成");
}

// ── 文件名安全处理 ───────────────────────────────────────────────
// 移除路径遍历字符，只保留纯文件名
std::string FileService::SanitizeFilename(const std::string& filename) {
    std::string result;

    // 1. 找到最后一个 '/' 或 '\'，只保留后面的纯文件名
    size_t pos = filename.find_last_of("/\\");
    if (pos != std::string::npos) {
        result = filename.substr(pos + 1);
    } else {
        result = filename;
    }

    // 2. 移除开头的点（防止隐藏文件，但保留扩展名前的点不处理）
    while (!result.empty() && result[0] == '.' && result.size() > 1
           && result[1] != '.') {
        result.erase(0, 1);
    }

    // 3. 移除控制字符和危险字符
    result.erase(std::remove_if(result.begin(), result.end(),
        [](char c) {
            return c < 0x20          // 控制字符
                || c == '/' || c == '\\'
                || c == ':' || c == '*'
                || c == '?' || c == '"'
                || c == '<' || c == '>'
                || c == '|';
        }), result.end());

    // 4. 空文件名 → 默认名
    if (result.empty()) {
        result = "unnamed";
    }

    // 5. 截断过长文件名（保留扩展名）
    if (result.size() > 200) {
        size_t dot = result.find_last_of('.');
        if (dot != std::string::npos && result.size() - dot <= 20) {
            result = result.substr(0, 200 - (result.size() - dot)) + result.substr(dot);
        } else {
            result = result.substr(0, 200);
        }
    }

    return result;
}

// ── 生成存储路径 ─────────────────────────────────────────────────
// 格式: {user_id}/{YYYYMMDD}_{HHmmss}_{sanitized_filename}
std::string FileService::GenerateFilepath(int64_t user_id,
                                           const std::string& filename) {
    // 获取当前时间
    auto now = std::time(nullptr);
    auto* tm = std::localtime(&now);

    std::ostringstream oss;
    oss << user_id << "/";

    // 时间戳前缀: YYYYMMDD_HHmmss
    oss << std::setfill('0')
        << std::setw(4) << (tm->tm_year + 1900)
        << std::setw(2) << (tm->tm_mon + 1)
        << std::setw(2) << tm->tm_mday
        << "_"
        << std::setw(2) << tm->tm_hour
        << std::setw(2) << tm->tm_min
        << std::setw(2) << tm->tm_sec
        << "_"
        << filename;

    return oss.str();
}

// ═══════════════════════════════════════════════════════════════════
// 上传初始化（支持断点续传）
//
// 状态机:
//   客户端 → upload_start
//     ├─ DB 中存在相同 user+filename+md5+filesize 且 status=0 ?
//     │   ├─ YES → 返回已有 file_id + upload_size (offset), is_resume=true
//     │   └─ NO  → 创建新记录, 返回 file_id + offset=0, is_resume=false
//     └─ 参数错误/DB错误 → file_id <= 0
// ═══════════════════════════════════════════════════════════════════
UploadStartResult FileService::UploadStart(int64_t user_id,
                                             const std::string& filename,
                                             int64_t filesize,
                                             const std::string& md5) {
    UploadStartResult result;

    if (filename.empty() || filesize <= 0) {
        LOG_WARNING("UploadStart 参数错误");
        result.file_id = -1;
        return result;
    }

    // 1. 清除文件名中的危险字符
    std::string safe_name = SanitizeFilename(filename);

    // ── 2. 断点续传检测 ──────────────────────────────────────────
    // 查找同用户、同文件名、同文件大小、同 MD5 的未完成上传
    auto existing = file_dao_->FindIncompleteByKey(
        user_id, safe_name, filesize, md5);

    if (existing) {
        // 磁盘上已有部分数据 → 续传
        result.file_id   = existing->id;
        result.offset    = existing->upload_size;
        result.is_resume = true;

        LOG_INFO("UploadStart [续传]: file_id=" + std::to_string(existing->id)
                 + ", already_uploaded=" + std::to_string(existing->upload_size)
                 + "/" + std::to_string(filesize)
                 + " (" + std::to_string(existing->upload_size * 100 / filesize) + "%)");

        return result;
    }

    // ── 3. 新上传 ────────────────────────────────────────────────
    storage_->CreateUserDir(user_id);

    std::string filepath = GenerateFilepath(user_id, safe_name);

    database::FileInfo file;
    file.user_id  = user_id;
    file.filename = safe_name;
    file.filepath = filepath;
    file.filesize = filesize;
    file.md5      = md5;
    file.status   = 0;

    int64_t file_id = file_dao_->Insert(file);
    if (file_id <= 0) {
        LOG_ERROR("UploadStart: DB 插入失败");
        result.file_id = -2;
        return result;
    }

    result.file_id   = file_id;
    result.offset    = 0;
    result.is_resume = false;

    LOG_INFO("UploadStart [新建]: file_id=" + std::to_string(file_id)
             + ", path=" + filepath
             + ", size=" + std::to_string(filesize));

    return result;
}

// ═══════════════════════════════════════════════════════════════════
// 上传数据块
// ═══════════════════════════════════════════════════════════════════
int64_t FileService::UploadData(int64_t file_id, int64_t user_id,
                                  const std::string& data, int64_t offset) {
    auto info = file_dao_->FindById(file_id);
    if (!info || info->user_id != user_id) return -1;
    if (!info->IsUploading()) return -1;

    // 按 filepath 写入磁盘
    int64_t written = storage_->WriteChunk(info->filepath, data.data(),
                                            data.size(), offset);
    if (written < 0) return -1;

    int64_t new_size = std::max(info->upload_size, offset + written);
    file_dao_->UpdateUploadSize(file_id, new_size);

    return written;
}

// ═══════════════════════════════════════════════════════════════════
// 完成上传
// ═══════════════════════════════════════════════════════════════════
bool FileService::UploadFinalize(int64_t file_id, int64_t user_id) {
    auto info = file_dao_->FindById(file_id);
    if (!info || info->user_id != user_id) return false;

    int64_t disk_size = storage_->GetFileSize(info->filepath);
    if (disk_size < info->filesize) {
        LOG_WARNING("UploadFinalize: 文件不完整, disk="
                    + std::to_string(disk_size)
                    + " < expected=" + std::to_string(info->filesize));
        return false;
    }

    file_dao_->UpdateStatus(file_id, 1);
    file_dao_->UpdateUploadSize(file_id, info->filesize);

    LOG_INFO("UploadFinalize: file_id=" + std::to_string(file_id)
             + " 完成, path=" + info->filepath);

    return true;
}

// ═══════════════════════════════════════════════════════════════════
// 下载 chunk
// ═══════════════════════════════════════════════════════════════════
std::string FileService::DownloadChunk(int64_t file_id, int64_t user_id,
                                         int64_t offset, size_t size) {
    auto info = file_dao_->FindById(file_id);
    if (!info || info->user_id != user_id || info->IsDeleted()) return "";

    return storage_->ReadChunk(info->filepath, offset, size);
}

// ═══════════════════════════════════════════════════════════════════
// 查询
// ═══════════════════════════════════════════════════════════════════
std::vector<database::FileInfoPtr> FileService::QueryFiles(int64_t user_id) {
    return file_dao_->FindByUserId(user_id);
}

database::FileInfoPtr FileService::GetFileInfo(int64_t file_id) {
    return file_dao_->FindById(file_id);
}

// ═══════════════════════════════════════════════════════════════════
// 删除
// ═══════════════════════════════════════════════════════════════════
bool FileService::Delete(int64_t file_id, int64_t user_id) {
    auto info = file_dao_->FindById(file_id);
    if (!info || info->user_id != user_id) return false;

    file_dao_->SoftDelete(file_id);
    storage_->DeleteFile(info->filepath);

    LOG_INFO("文件已删除: id=" + std::to_string(file_id)
             + ", path=" + info->filepath);
    return true;
}

}  // namespace services
}  // namespace fileserver
