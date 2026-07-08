#include "storage/storage_manager.h"
#include "common/logger.h"

#include <fstream>
#include <sstream>
#include <cstdio>
#include <sys/stat.h>

namespace fileserver {
namespace storage {

StorageManager::StorageManager(const std::string& base_path)
    : base_path_(base_path)
{
    // 创建存储目录 (如果不存在)
    mkdir(base_path_.c_str(), 0755);

    LOG_INFO("StorageManager 初始化: base_path=" + base_path_);
}

// ── 获取文件路径 ─────────────────────────────────────────────────
std::string StorageManager::GetFilePath(int64_t file_id) const {
    std::ostringstream oss;
    oss << base_path_ << "/file_" << file_id << ".dat";
    return oss.str();
}

// ── 写入 chunk ───────────────────────────────────────────────────
// 使用 fstream 定位写入，不加载整个文件
int64_t StorageManager::WriteChunk(int64_t file_id, const char* data,
                                    size_t len, int64_t offset) {
    std::string filepath = GetFilePath(file_id);

    // 以读写模式打开 (文件不存在则创建)
    std::fstream fs(filepath, std::ios::in | std::ios::out
                              | std::ios::binary | std::ios::ate);
    if (!fs.is_open()) {
        // 文件不存在，先创建
        fs.clear();
        fs.open(filepath, std::ios::out | std::ios::binary);
        fs.close();
        fs.open(filepath, std::ios::in | std::ios::out | std::ios::binary);
    }

    if (!fs.is_open()) {
        LOG_ERROR("无法打开文件写入: " + filepath);
        return -1;
    }

    // Seek 到 offset
    fs.seekp(offset, std::ios::beg);
    if (fs.fail()) {
        LOG_ERROR("文件 seek 失败: " + filepath + " offset=" + std::to_string(offset));
        return -1;
    }

    // 写入数据
    fs.write(data, len);
    if (fs.fail()) {
        LOG_ERROR("文件写入失败: " + filepath);
        return -1;
    }

    fs.close();

    return static_cast<int64_t>(len);
}

// ── 读取 chunk ───────────────────────────────────────────────────
// 只读取请求的 chunk 大小，不加载整个文件
std::string StorageManager::ReadChunk(int64_t file_id,
                                       int64_t offset, size_t size) {
    std::string filepath = GetFilePath(file_id);

    std::ifstream fs(filepath, std::ios::binary);
    if (!fs.is_open()) {
        LOG_ERROR("无法打开文件读取: " + filepath);
        return "";
    }

    // 获取文件大小
    fs.seekg(0, std::ios::end);
    int64_t file_size = fs.tellg();

    // Seek 到请求的 offset
    fs.seekg(offset, std::ios::beg);
    if (fs.fail()) {
        LOG_ERROR("文件 seek 失败: " + filepath);
        return "";
    }

    // 实际读取大小不能超过文件剩余长度
    int64_t remaining = file_size - offset;
    if (remaining <= 0) return "";
    if (static_cast<int64_t>(size) > remaining) {
        size = static_cast<size_t>(remaining);
    }

    // 分配缓冲区并读取
    std::string buffer(size, '\0');
    fs.read(&buffer[0], size);
    buffer.resize(fs.gcount());  // 实际读取量

    fs.close();
    return buffer;
}

// ── 删除文件 ─────────────────────────────────────────────────────
bool StorageManager::DeleteFile(int64_t file_id) {
    std::string filepath = GetFilePath(file_id);
    if (std::remove(filepath.c_str()) != 0) {
        LOG_WARNING("文件删除失败 (可能不存在): " + filepath);
        return false;
    }
    LOG_INFO("文件已删除: " + filepath);
    return true;
}

// ── 获取文件大小 ─────────────────────────────────────────────────
int64_t StorageManager::GetFileSize(int64_t file_id) {
    std::string filepath = GetFilePath(file_id);
    struct stat st;
    if (stat(filepath.c_str(), &st) != 0) {
        return -1;
    }
    return st.st_size;
}

}  // namespace storage
}  // namespace fileserver
