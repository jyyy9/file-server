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
    // 创建存储根目录（如果不存在）
    mkdir(base_path_.c_str(), 0755);

    LOG_INFO("StorageManager 初始化: base_path=" + base_path_);
}

// ── 构建绝对路径 ─────────────────────────────────────────────────
std::string StorageManager::GetAbsolutePath(const std::string& filepath) const {
    if (filepath.empty()) return "";
    // 确保没有双斜杠
    if (base_path_.back() == '/' || filepath.front() == '/') {
        return base_path_ + filepath;
    }
    return base_path_ + "/" + filepath;
}

// ── 创建用户目录 ─────────────────────────────────────────────────
bool StorageManager::CreateUserDir(int64_t user_id) {
    std::ostringstream oss;
    oss << base_path_ << "/" << user_id;
    std::string dir = oss.str();

    if (mkdir(dir.c_str(), 0755) != 0) {
        // EEXIST 不算错误
        if (errno != EEXIST) {
            LOG_ERROR("创建用户目录失败: " + dir + " errno=" + std::to_string(errno));
            return false;
        }
    }
    return true;
}

// ── 写入 chunk ───────────────────────────────────────────────────
int64_t StorageManager::WriteChunk(const std::string& filepath,
                                    const char* data,
                                    size_t len, int64_t offset) {
    std::string abspath = GetAbsolutePath(filepath);

    // 以读写模式打开（文件不存在则创建）
    std::fstream fs(abspath, std::ios::in | std::ios::out
                            | std::ios::binary | std::ios::ate);
    if (!fs.is_open()) {
        // 文件不存在，先创建
        fs.clear();
        fs.open(abspath, std::ios::out | std::ios::binary);
        fs.close();
        fs.open(abspath, std::ios::in | std::ios::out | std::ios::binary);
    }

    if (!fs.is_open()) {
        LOG_ERROR("无法打开文件写入: " + abspath);
        return -1;
    }

    // Seek 到 offset
    fs.seekp(offset, std::ios::beg);
    if (fs.fail()) {
        LOG_ERROR("文件 seek 失败: " + abspath + " offset=" + std::to_string(offset));
        return -1;
    }

    // 写入数据
    fs.write(data, len);
    if (fs.fail()) {
        LOG_ERROR("文件写入失败: " + abspath);
        return -1;
    }

    fs.close();
    return static_cast<int64_t>(len);
}

// ── 读取 chunk ───────────────────────────────────────────────────
std::string StorageManager::ReadChunk(const std::string& filepath,
                                       int64_t offset, size_t size) {
    std::string abspath = GetAbsolutePath(filepath);

    std::ifstream fs(abspath, std::ios::binary);
    if (!fs.is_open()) {
        LOG_ERROR("无法打开文件读取: " + abspath);
        return "";
    }

    // 获取文件大小
    fs.seekg(0, std::ios::end);
    int64_t file_size = fs.tellg();

    // Seek 到请求的 offset
    fs.seekg(offset, std::ios::beg);
    if (fs.fail()) {
        LOG_ERROR("文件 seek 失败: " + abspath);
        return "";
    }

    // 实际读取大小不能超过文件剩余长度
    int64_t remaining = file_size - offset;
    if (remaining <= 0) return "";
    if (static_cast<int64_t>(size) > remaining) {
        size = static_cast<size_t>(remaining);
    }

    std::string buffer(size, '\0');
    fs.read(&buffer[0], size);
    buffer.resize(fs.gcount());

    fs.close();
    return buffer;
}

// ── 删除文件 ─────────────────────────────────────────────────────
bool StorageManager::DeleteFile(const std::string& filepath) {
    std::string abspath = GetAbsolutePath(filepath);
    if (std::remove(abspath.c_str()) != 0) {
        LOG_WARNING("文件删除失败 (可能不存在): " + abspath);
        return false;
    }
    LOG_INFO("文件已删除: " + abspath);
    return true;
}

// ── 获取文件大小 ─────────────────────────────────────────────────
int64_t StorageManager::GetFileSize(const std::string& filepath) {
    std::string abspath = GetAbsolutePath(filepath);
    struct stat st;
    if (stat(abspath.c_str(), &st) != 0) {
        return -1;
    }
    return st.st_size;
}

}  // namespace storage
}  // namespace fileserver
