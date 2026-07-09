#include <iostream>
#include <fstream>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <random>
#include <iomanip>
#include <sstream>

#include <openssl/md5.h>

#include "database/mysql_pool.h"
#include "database/file_dao.h"
#include "storage/storage_manager.h"
#include "services/file_service.h"
#include "common/logger.h"

using namespace fileserver;
using namespace fileserver::database;
using namespace fileserver::storage;
using namespace fileserver::services;

// ── 配置 ─────────────────────────────────────────────────────────
const char*  kDbHost     = "127.0.0.1";
const int    kDbPort     = 3306;
const char*  kDbUser     = "root";
const char*  kDbPassword = "123456";
const char*  kDbName     = "fileserver";
const char*  kStorageDir = "./storage/data";
const int64_t kTestFileSize = 10 * 1024 * 1024;  // 10 MB
const int64_t kChunkSize    = 4 * 1024 * 1024;   // 4 MB

// ── 计算文件 MD5 ─────────────────────────────────────────────────
static std::string FileMD5(const std::string& filepath) {
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5_CTX ctx;
    MD5_Init(&ctx);

    std::ifstream fs(filepath, std::ios::binary);
    char buf[8192];
    while (fs.read(buf, sizeof(buf)) || fs.gcount() > 0) {
        MD5_Update(&ctx, buf, fs.gcount());
    }

    MD5_Final(digest, &ctx);

    std::ostringstream oss;
    for (int i = 0; i < MD5_DIGEST_LENGTH; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(digest[i]);
    }
    return oss.str();
}

// ── 创建测试文件 ─────────────────────────────────────────────────
static std::string CreateTestFile(const std::string& path, int64_t size) {
    std::ofstream fs(path, std::ios::binary);
    std::mt19937 rng(42);  // 固定种子，可复现
    char buf[65536];
    int64_t written = 0;
    while (written < size) {
        int64_t remain = size - written;
        size_t batch = (remain > 65536) ? 65536 : static_cast<size_t>(remain);
        for (size_t i = 0; i < batch; ++i) {
            buf[i] = static_cast<char>(rng() & 0xFF);
        }
        fs.write(buf, batch);
        written += batch;
    }
    fs.close();
    return path;
}

// ── 建表 ─────────────────────────────────────────────────────────
static void CreateTable(MysqlPool* pool) {
    auto conn = pool->GetConnection();
    if (!conn) return;
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS file_info (
            id          INT PRIMARY KEY AUTO_INCREMENT,
            user_id     INT NOT NULL,
            filename    VARCHAR(255) NOT NULL,
            filepath    VARCHAR(512),
            filesize    BIGINT DEFAULT 0,
            upload_size BIGINT DEFAULT 0,
            md5         VARCHAR(64),
            status      INT DEFAULT 0,
            create_time DATETIME DEFAULT CURRENT_TIMESTAMP
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
    )";
    mysql_query(conn.get(), sql);
    std::cout << "file_info 表已就绪" << std::endl;
}

// ═══════════════════════════════════════════════════════════════════
int main() {
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "  FileServer – 文件管理服务测试" << std::endl;
    std::cout << std::string(60, '=') << "\n" << std::endl;

    // ── 初始化 ──────────────────────────────────────────────────
    MysqlPool pool(kDbHost, kDbPort, kDbUser, kDbPassword, kDbName, 4);
    CreateTable(&pool);

    FileDAO file_dao(&pool);
    StorageManager storage(kStorageDir);
    FileService file_svc(&file_dao, &storage);

    const int64_t kTestUserId = 1;  // 假设测试用户 id=1

    int passed = 0, failed = 0;
    int64_t file_id = 0;
    std::string original_md5;
    std::string test_file_path = std::string(kStorageDir) + "/_test_original.dat";

    // ═══════════════════════════════════════════════════════════════
    // 测试 1: 创建 10MB 测试文件 + 计算 MD5
    // ═══════════════════════════════════════════════════════════════
    std::cout << "\n=== 测试 1: 创建测试文件 (" << (kTestFileSize >> 20) << " MB) ===" << std::endl;
    {
        CreateTestFile(test_file_path, kTestFileSize);
        original_md5 = FileMD5(test_file_path);
        std::cout << "  文件路径: " << test_file_path << std::endl;
        std::cout << "  MD5:      " << original_md5 << std::endl;
        std::cout << "  [通过] 测试文件创建完成" << std::endl;
        passed++;
    }

    // ═══════════════════════════════════════════════════════════════
    // 测试 2: UploadStart (新上传)
    // ═══════════════════════════════════════════════════════════════
    std::cout << "\n=== 测试 2: UploadStart ===" << std::endl;
    {
        auto result = file_svc.UploadStart(kTestUserId, "test_10mb.dat",
                                             kTestFileSize, original_md5);
        file_id = result.file_id;
        if (file_id > 0 && !result.is_resume) {
            std::cout << "  file_id: " << file_id
                      << ", offset: " << result.offset
                      << ", 新文件" << std::endl;
            std::cout << "  [通过] 上传初始化成功" << std::endl;
            passed++;
        } else {
            std::cout << "  [失败] file_id=" << file_id << std::endl;
            failed++;
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 测试 2.5: 验证存储路径结构
    // ═══════════════════════════════════════════════════════════════
    std::cout << "\n=== 测试 2.5: 验证路径结构 ===" << std::endl;
    {
        auto info = file_svc.GetFileInfo(file_id);
        if (info) {
            std::cout << "  DB filepath: " << info->filepath << std::endl;
            // 格式应为: 1/YYYYMMDD_HHmmss_test_10mb.dat
            bool path_ok = (info->filepath.find(std::to_string(kTestUserId) + "/") == 0
                         && info->filepath.find("test_10mb.dat") != std::string::npos);
            if (path_ok) {
                std::cout << "  [通过] 路径格式正确" << std::endl;
                passed++;
            } else {
                std::cout << "  [失败] 路径格式不正确" << std::endl;
                failed++;
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 测试 2.6: 文件名 sanitize（路径遍历攻击防护）
    // ═══════════════════════════════════════════════════════════════
    std::cout << "\n=== 测试 2.6: 危险文件名过滤 ===" << std::endl;
    {
        auto evil_result = file_svc.UploadStart(kTestUserId, "../../etc/passwd",
                                                 1024, "dummy");
        auto evil_info = file_svc.GetFileInfo(evil_result.file_id);
        if (evil_info && evil_info->filepath.find("..") == std::string::npos
                      && evil_info->filepath.find("etc") == std::string::npos) {
            std::cout << "  sanitized: " << evil_info->filename << std::endl;
            std::cout << "  [通过] 路径遍历被过滤" << std::endl;
            passed++;
            // 清理
            file_svc.Delete(evil_result.file_id, kTestUserId);
        } else {
            std::cout << "  [失败] 危险路径未被过滤" << std::endl;
            failed++;
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 测试 3: UploadData 分块上传 (4MB chunk)
    // ═══════════════════════════════════════════════════════════════
    std::cout << "\n=== 测试 3: 分块上传 (4MB/chunk) ===" << std::endl;
    {
        std::ifstream fs(test_file_path, std::ios::binary);
        int64_t offset = 0;
        int chunks = 0;
        std::string buffer;

        while (offset < kTestFileSize) {
            int64_t remain = kTestFileSize - offset;
            size_t read_size = (remain > kChunkSize) ? kChunkSize : remain;
            buffer.resize(read_size);
            fs.read(&buffer[0], read_size);

            int64_t written = file_svc.UploadData(file_id, kTestUserId,
                                                    buffer, offset);
            if (written != static_cast<int64_t>(read_size)) {
                std::cout << "  [失败] chunk " << chunks
                          << " 写入失败: expected=" << read_size
                          << ", written=" << written << std::endl;
                failed++;
                break;
            }
            offset += written;
            chunks++;
        }
        fs.close();

        if (offset == kTestFileSize) {
            std::cout << "  Chunks: " << chunks
                      << ", 总上传: " << (offset >> 20) << " MB" << std::endl;
            std::cout << "  [通过] 分块上传完成" << std::endl;
            passed++;
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 测试 4: UploadFinalize
    // ═══════════════════════════════════════════════════════════════
    std::cout << "\n=== 测试 4: UploadFinalize ===" << std::endl;
    {
        bool ok = file_svc.UploadFinalize(file_id, kTestUserId);
        if (ok) {
            auto info = file_svc.GetFileInfo(file_id);
            if (info && info->status == 1) {
                std::cout << "  status: " << info->status << " (完成)" << std::endl;
                std::cout << "  [通过] 上传完成" << std::endl;
                passed++;
            } else {
                std::cout << "  [失败] status 未更新" << std::endl;
                failed++;
            }
        } else {
            std::cout << "  [失败] finalize 返回 false" << std::endl;
            failed++;
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 测试 5: 下载并校验 MD5
    // ═══════════════════════════════════════════════════════════════
    std::cout << "\n=== 测试 5: 下载 + MD5 校验 ===" << std::endl;
    {
        std::string downloaded_path = std::string(kStorageDir) + "/_test_downloaded.dat";
        std::ofstream out(downloaded_path, std::ios::binary);

        int64_t offset = 0;
        int chunks = 0;
        while (offset < kTestFileSize) {
            size_t read_size = kChunkSize;
            std::string data = file_svc.DownloadChunk(file_id, kTestUserId,
                                                        offset, read_size);
            if (data.empty()) break;

            out.write(data.data(), data.size());
            offset += data.size();
            chunks++;
        }
        out.close();

        std::string downloaded_md5 = FileMD5(downloaded_path);

        std::cout << "  Chunks: " << chunks << std::endl;
        std::cout << "  原始 MD5: " << original_md5 << std::endl;
        std::cout << "  下载 MD5: " << downloaded_md5 << std::endl;

        if (original_md5 == downloaded_md5) {
            std::cout << "  [通过] MD5 校验通过" << std::endl;
            passed++;
        } else {
            std::cout << "  [失败] MD5 不匹配" << std::endl;
            failed++;
        }

        // 清理下载文件
        std::remove(downloaded_path.c_str());
    }

    // ═══════════════════════════════════════════════════════════════
    // 测试 6: 查询文件列表
    // ═══════════════════════════════════════════════════════════════
    std::cout << "\n=== 测试 6: 查询文件列表 ===" << std::endl;
    {
        auto files = file_svc.QueryFiles(kTestUserId);
        bool found = false;
        for (auto& f : files) {
            if (f->id == file_id) {
                found = true;
                std::cout << "  找到: id=" << f->id
                          << ", name=" << f->filename
                          << ", size=" << f->filesize
                          << ", status=" << f->status << std::endl;
            }
        }
        std::cout << "  文件总数: " << files.size() << std::endl;
        if (found) {
            std::cout << "  [通过] 文件在列表中" << std::endl;
            passed++;
        } else {
            std::cout << "  [失败] 文件不在列表中" << std::endl;
            failed++;
        }
    }

    // 测试 7 已移除 —— finalize 后不允许写入，断点续传由 test 10 覆盖

    // ═══════════════════════════════════════════════════════════════
    // 测试 8: 删除文件
    // ═══════════════════════════════════════════════════════════════
    std::cout << "\n=== 测试 8: 删除文件 ===" << std::endl;
    {
        bool ok = file_svc.Delete(file_id, kTestUserId);
        auto info = file_svc.GetFileInfo(file_id);
        if (ok && info && info->IsDeleted()) {
            std::cout << "  [通过] 软删除成功 (status=" << info->status << ")" << std::endl;
            passed++;
        } else {
            std::cout << "  [失败] 删除失败" << std::endl;
            failed++;
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 测试 9: 删除后查询列表不包含
    // ═══════════════════════════════════════════════════════════════
    std::cout << "\n=== 测试 9: 删除后不在列表中 ===" << std::endl;
    {
        auto files = file_svc.QueryFiles(kTestUserId);
        bool found = false;
        for (auto& f : files) {
            if (f->id == file_id) found = true;
        }
        if (!found) {
            std::cout << "  [通过] 已删除文件不出现在列表中" << std::endl;
            passed++;
        } else {
            std::cout << "  [失败]" << std::endl;
            failed++;
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 测试 10: 断点续传（50%后断开，重新上传）
    // ═══════════════════════════════════════════════════════════════
    std::cout << "\n=== 测试 10: 断点续传 ===" << std::endl;
    {
        const int64_t kResumeFileSize = 100 * 1024 * 1024;  // 100 MB
        std::string resume_file = std::string(kStorageDir) + "/_resume_test.dat";
        CreateTestFile(resume_file, kResumeFileSize);
        std::string resume_md5 = FileMD5(resume_file);

        // ── 第一次上传: 只传 50% ──────────────────────────────
        auto r1 = file_svc.UploadStart(kTestUserId, "resume_test.dat",
                                         kResumeFileSize, resume_md5);
        int64_t resume_id = r1.file_id;
        std::cout << "  第1次 UploadStart: file_id=" << resume_id
                  << ", offset=" << r1.offset
                  << ", is_resume=" << (r1.is_resume ? "yes" : "no") << std::endl;

        // 只上传前 50MB
        std::ifstream fs(resume_file, std::ios::binary);
        int64_t half_size = kResumeFileSize / 2;
        int64_t offset = 0;
        std::string buf;
        while (offset < half_size) {
            size_t read_size = (offset + kChunkSize > half_size)
                               ? (half_size - offset) : kChunkSize;
            buf.resize(read_size);
            fs.read(&buf[0], read_size);
            file_svc.UploadData(resume_id, kTestUserId, buf, offset);
            offset += read_size;
        }
        fs.close();

        auto info_half = file_svc.GetFileInfo(resume_id);
        std::cout << "  已上传: " << (info_half->upload_size >> 20) << " MB / "
                  << (info_half->filesize >> 20) << " MB ("
                  << (info_half->upload_size * 100 / info_half->filesize) << "%)" << std::endl;

        // ── 模拟断开: 不调 finalize ───────────────────────────

        // ── 第二次 UploadStart: 应检测到续传 ─────────────────
        auto r2 = file_svc.UploadStart(kTestUserId, "resume_test.dat",
                                         kResumeFileSize, resume_md5);

        std::cout << "  第2次 UploadStart: file_id=" << r2.file_id
                  << ", offset=" << r2.offset
                  << ", is_resume=" << (r2.is_resume ? "yes" : "no") << std::endl;

        bool resume_ok = (r2.is_resume
                       && r2.file_id == resume_id
                       && r2.offset == half_size);

        if (resume_ok) {
            std::cout << "  [通过] 检测到未完成任务，offset=" << (r2.offset >> 20) << " MB" << std::endl;
            passed++;
        } else {
            std::cout << "  [失败] 续传检测失败" << std::endl;
            failed++;
        }

        // ── 继续上传剩余 50% ──────────────────────────────────
        if (resume_ok) {
            fs.open(resume_file, std::ios::binary);
            fs.seekg(half_size);
            offset = half_size;
            while (offset < kResumeFileSize) {
                size_t read_size = (offset + kChunkSize > kResumeFileSize)
                                   ? (kResumeFileSize - offset) : kChunkSize;
                buf.resize(read_size);
                fs.read(&buf[0], read_size);
                file_svc.UploadData(resume_id, kTestUserId, buf, offset);
                offset += read_size;
            }
            fs.close();

            // 完成上传
            file_svc.UploadFinalize(resume_id, kTestUserId);

            auto final_info = file_svc.GetFileInfo(resume_id);
            std::cout << "  续传后: upload_size=" << (final_info->upload_size >> 20)
                      << " MB, status=" << final_info->status << std::endl;

            // ── 下载校验 MD5 ──────────────────────────────────
            std::string dl_path = std::string(kStorageDir) + "/_resume_dl.dat";
            std::ofstream out(dl_path, std::ios::binary);
            offset = 0;
            while (offset < kResumeFileSize) {
                std::string data = file_svc.DownloadChunk(resume_id, kTestUserId,
                                                            offset, kChunkSize);
                if (data.empty()) break;
                out.write(data.data(), data.size());
                offset += data.size();
            }
            out.close();

            std::string dl_md5 = FileMD5(dl_path);
            if (dl_md5 == resume_md5) {
                std::cout << "  [通过] 断点续传后 MD5 校验一致" << std::endl;
                passed++;
            } else {
                std::cout << "  [失败] MD5 不匹配" << std::endl;
                failed++;
            }

            // 清理
            file_svc.Delete(resume_id, kTestUserId);
            std::remove(dl_path.c_str());
        }

        std::remove(resume_file.c_str());
    }

    // ── 清理 ─────────────────────────────────────────────────────
    std::remove(test_file_path.c_str());

    // ── 总结 ─────────────────────────────────────────────────────
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "  测试结果: " << passed << " 通过, " << failed << " 失败" << std::endl;
    if (failed == 0) {
        std::cout << "  全部测试通过!" << std::endl;
    }
    std::cout << std::string(60, '=') << "\n" << std::endl;

    return failed > 0 ? 1 : 0;
}
