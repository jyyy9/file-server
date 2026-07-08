#ifndef FILESERVER_DATABASE_FILE_DAO_H_
#define FILESERVER_DATABASE_FILE_DAO_H_

#include <vector>
#include <string>
#include <memory>

#include "database/file_info.h"
#include "database/mysql_pool.h"
#include "common/common.h"

namespace fileserver {
namespace database {

// ── 文件元数据 DAO ───────────────────────────────────────────────
//
// 封装 file_info 表的所有 SQL 操作。
// 全部使用 Prepared Statement 防 SQL 注入。
class FileDAO {
public:
    explicit FileDAO(MysqlPool* pool);

    FILESERVER_DISALLOW_COPY_AND_MOVE(FileDAO);

    // ── CRUD ─────────────────────────────────────────────────────

    // 插入文件记录 (upload_start)
    int64_t Insert(const FileInfo& file);

    // 按 ID 查询
    FileInfoPtr FindById(int64_t file_id);

    // 查询用户的所有文件 (不含已删除)
    std::vector<FileInfoPtr> FindByUserId(int64_t user_id);

    // 更新上传进度
    bool UpdateUploadSize(int64_t file_id, int64_t upload_size);

    // 更新文件状态
    bool UpdateStatus(int64_t file_id, int32_t status);

    // 软删除 (status = 2)
    bool SoftDelete(int64_t file_id);

private:
    MysqlPool* pool_;
};

}  // namespace database
}  // namespace fileserver

#endif  // FILESERVER_DATABASE_FILE_DAO_H_
