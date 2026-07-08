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
const char*  kStorageDir = "./storage_data";
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
    // 测试 2: UploadStart
    // ═══════════════════════════════════════════════════════════════
    std::cout << "\n=== 测试 2: UploadStart ===" << std::endl;
    {
        file_id = file_svc.UploadStart(kTestUserId, "test_10mb.dat",
                                         kTestFileSize, original_md5);
        if (file_id > 0) {
            std::cout << "  file_id: " << file_id << std::endl;
            std::cout << "  [通过] 上传初始化成功" << std::endl;
            passed++;
        } else {
            std::cout << "  [失败] 上传初始化失败, code=" << file_id << std::endl;
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

    // ═══════════════════════════════════════════════════════════════
    // 测试 7: 断点续传验证 (重复上传同一 chunk 应成功)
    // ═══════════════════════════════════════════════════════════════
    std::cout << "\n=== 测试 7: 断点续传覆盖写 ===" << std::endl;
    {
        // 重新上传第一个 chunk 的相同数据
        std::ifstream fs(test_file_path, std::ios::binary);
        std::string buf(kChunkSize, '\0');
        fs.read(&buf[0], kChunkSize);
        fs.close();

        int64_t written = file_svc.UploadData(file_id, kTestUserId, buf, 0);
        if (written == kChunkSize) {
            std::cout << "  [通过] 覆盖写入成功" << std::endl;
            passed++;
        } else {
            std::cout << "  [失败]" << std::endl;
            failed++;
        }
    }

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
