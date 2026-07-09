#include "database/file_dao.h"
#include "common/logger.h"

#include <mysql/mysql.h>
#include <cstring>

namespace fileserver {
namespace database {

FileDAO::FileDAO(MysqlPool* pool)
    : pool_(pool) {}

// ── 插入 ─────────────────────────────────────────────────────────
int64_t FileDAO::Insert(const FileInfo& file) {
    auto conn = pool_->GetConnection();
    if (!conn) return -1;

    const char* sql =
        "INSERT INTO file_info (user_id, filename, filepath, filesize, md5, upload_size, status) "
        "VALUES (?, ?, ?, ?, ?, 0, 0)";

    MYSQL_STMT* stmt = mysql_stmt_init(conn.get());
    if (!stmt) return -1;

    mysql_stmt_prepare(stmt, sql, std::strlen(sql));

    int64_t  user_id  = file.user_id;
    int64_t  filesize = file.filesize;
    unsigned long fn_len = file.filename.size();
    unsigned long fp_len = file.filepath.size();
    unsigned long md5_len = file.md5.size();

    MYSQL_BIND bind[5];
    std::memset(bind, 0, sizeof(bind));

    bind[0].buffer_type   = MYSQL_TYPE_LONGLONG;
    bind[0].buffer        = &user_id;

    bind[1].buffer_type   = MYSQL_TYPE_STRING;
    bind[1].buffer        = const_cast<char*>(file.filename.c_str());
    bind[1].buffer_length = fn_len;
    bind[1].length        = &fn_len;

    bind[2].buffer_type   = MYSQL_TYPE_STRING;
    bind[2].buffer        = const_cast<char*>(file.filepath.c_str());
    bind[2].buffer_length = fp_len;
    bind[2].length        = &fp_len;

    bind[3].buffer_type   = MYSQL_TYPE_LONGLONG;
    bind[3].buffer        = &filesize;

    bind[4].buffer_type   = MYSQL_TYPE_STRING;
    bind[4].buffer        = const_cast<char*>(file.md5.c_str());
    bind[4].buffer_length = md5_len;
    bind[4].length        = &md5_len;

    if (mysql_stmt_bind_param(stmt, bind) != 0 ||
        mysql_stmt_execute(stmt) != 0) {
        LOG_ERROR(std::string("FileDAO::Insert 失败: ") + mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }

    int64_t id = mysql_stmt_insert_id(stmt);
    mysql_stmt_close(stmt);
    return id;
}

// ── 按ID查询 ────────────────────────────────────────────────────
FileInfoPtr FileDAO::FindById(int64_t file_id) {
    auto conn = pool_->GetConnection();
    if (!conn) return nullptr;

    const char* sql = "SELECT id, user_id, filename, filepath, filesize, "
                      "upload_size, md5, status, create_time "
                      "FROM file_info WHERE id = ?";

    MYSQL_STMT* stmt = mysql_stmt_init(conn.get());
    mysql_stmt_prepare(stmt, sql, std::strlen(sql));

    MYSQL_BIND bind[1];
    std::memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer      = &file_id;

    mysql_stmt_bind_param(stmt, bind);
    mysql_stmt_execute(stmt);

    // 结果缓冲区
    int64_t id=0, user_id=0, filesize=0, upload_size=0;
    int32_t status=0;
    char fn[256]={0}, fp[512]={0}, md5[65]={0}, ct[32]={0};
    unsigned long fn_len, fp_len, md5_len, ct_len;

    MYSQL_BIND result[9];
    std::memset(result, 0, sizeof(result));
    result[0].buffer_type = MYSQL_TYPE_LONGLONG;  result[0].buffer = &id;
    result[1].buffer_type = MYSQL_TYPE_LONGLONG;  result[1].buffer = &user_id;
    result[2].buffer_type = MYSQL_TYPE_STRING;    result[2].buffer = fn;  result[2].buffer_length = 255; result[2].length = &fn_len;
    result[3].buffer_type = MYSQL_TYPE_STRING;    result[3].buffer = fp;  result[3].buffer_length = 511; result[3].length = &fp_len;
    result[4].buffer_type = MYSQL_TYPE_LONGLONG;  result[4].buffer = &filesize;
    result[5].buffer_type = MYSQL_TYPE_LONGLONG;  result[5].buffer = &upload_size;
    result[6].buffer_type = MYSQL_TYPE_STRING;    result[6].buffer = md5; result[6].buffer_length = 64;  result[6].length = &md5_len;
    result[7].buffer_type = MYSQL_TYPE_LONG;      result[7].buffer = &status;
    result[8].buffer_type = MYSQL_TYPE_STRING;    result[8].buffer = ct;  result[8].buffer_length = 31;  result[8].length = &ct_len;

    mysql_stmt_bind_result(stmt, result);
    mysql_stmt_store_result(stmt);

    FileInfoPtr info;
    if (mysql_stmt_fetch(stmt) == 0) {
        fn[fn_len]='\0'; fp[fp_len]='\0'; md5[md5_len]='\0'; ct[ct_len]='\0';
        info = std::make_shared<FileInfo>();
        info->id=id; info->user_id=user_id; info->filename=fn;
        info->filepath=fp; info->filesize=filesize; info->upload_size=upload_size;
        info->md5=md5; info->status=status; info->create_time=ct;
    }
    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
    return info;
}

// ── 按用户ID查询 ─────────────────────────────────────────────────
std::vector<FileInfoPtr> FileDAO::FindByUserId(int64_t user_id) {
    std::vector<FileInfoPtr> result;
    auto conn = pool_->GetConnection();
    if (!conn) return result;

    const char* sql = "SELECT id, user_id, filename, filepath, filesize, "
                      "upload_size, md5, status, create_time "
                      "FROM file_info WHERE user_id = ? AND status != 2 "
                      "ORDER BY id DESC";

    MYSQL_STMT* stmt = mysql_stmt_init(conn.get());
    mysql_stmt_prepare(stmt, sql, std::strlen(sql));

    MYSQL_BIND bind[1];
    std::memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer      = &user_id;

    mysql_stmt_bind_param(stmt, bind);
    mysql_stmt_execute(stmt);

    int64_t id=0, uid=0, filesize=0, upload_size=0;
    int32_t status=0;
    char fn[256]={0}, fp[512]={0}, md5[65]={0}, ct[32]={0};
    unsigned long fn_len, fp_len, md5_len, ct_len;

    MYSQL_BIND res[9];
    std::memset(res, 0, sizeof(res));
    res[0].buffer_type = MYSQL_TYPE_LONGLONG; res[0].buffer = &id;
    res[1].buffer_type = MYSQL_TYPE_LONGLONG; res[1].buffer = &uid;
    res[2].buffer_type = MYSQL_TYPE_STRING;   res[2].buffer = fn;  res[2].buffer_length=255; res[2].length=&fn_len;
    res[3].buffer_type = MYSQL_TYPE_STRING;   res[3].buffer = fp;  res[3].buffer_length=511; res[3].length=&fp_len;
    res[4].buffer_type = MYSQL_TYPE_LONGLONG; res[4].buffer = &filesize;
    res[5].buffer_type = MYSQL_TYPE_LONGLONG; res[5].buffer = &upload_size;
    res[6].buffer_type = MYSQL_TYPE_STRING;   res[6].buffer = md5; res[6].buffer_length=64;  res[6].length=&md5_len;
    res[7].buffer_type = MYSQL_TYPE_LONG;     res[7].buffer = &status;
    res[8].buffer_type = MYSQL_TYPE_STRING;   res[8].buffer = ct;  res[8].buffer_length=31;  res[8].length=&ct_len;

    mysql_stmt_bind_result(stmt, res);
    mysql_stmt_store_result(stmt);

    while (mysql_stmt_fetch(stmt) == 0) {
        fn[fn_len]='\0'; fp[fp_len]='\0'; md5[md5_len]='\0'; ct[ct_len]='\0';
        auto info = std::make_shared<FileInfo>();
        info->id=id; info->user_id=uid; info->filename=fn;
        info->filepath=fp; info->filesize=filesize; info->upload_size=upload_size;
        info->md5=md5; info->status=status; info->create_time=ct;
        result.push_back(info);
    }
    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
    return result;
}

// ── 更新上传进度 ─────────────────────────────────────────────────
bool FileDAO::UpdateUploadSize(int64_t file_id, int64_t upload_size) {
    auto conn = pool_->GetConnection();
    if (!conn) return false;

    const char* sql = "UPDATE file_info SET upload_size = ? WHERE id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn.get());
    mysql_stmt_prepare(stmt, sql, std::strlen(sql));

    MYSQL_BIND bind[2];
    std::memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG; bind[0].buffer = &upload_size;
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG; bind[1].buffer = &file_id;

    bool ok = (mysql_stmt_bind_param(stmt, bind) == 0 &&
               mysql_stmt_execute(stmt) == 0);
    mysql_stmt_close(stmt);
    return ok;
}

// ── 更新状态 ─────────────────────────────────────────────────────
bool FileDAO::UpdateStatus(int64_t file_id, int32_t status) {
    auto conn = pool_->GetConnection();
    if (!conn) return false;

    const char* sql = "UPDATE file_info SET status = ? WHERE id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn.get());
    mysql_stmt_prepare(stmt, sql, std::strlen(sql));

    MYSQL_BIND bind[2];
    std::memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONG;     bind[0].buffer = &status;
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG; bind[1].buffer = &file_id;

    bool ok = (mysql_stmt_bind_param(stmt, bind) == 0 &&
               mysql_stmt_execute(stmt) == 0);
    mysql_stmt_close(stmt);
    return ok;
}

// ── 软删除 ───────────────────────────────────────────────────────
bool FileDAO::SoftDelete(int64_t file_id) {
    return UpdateStatus(file_id, 2);
}

// ── 查找未完成的上传（断点续传匹配）──────────────────────────────
FileInfoPtr FileDAO::FindIncompleteByKey(int64_t user_id,
                                          const std::string& filename,
                                          int64_t filesize,
                                          const std::string& md5) {
    auto conn = pool_->GetConnection();
    if (!conn) return nullptr;

    const char* sql =
        "SELECT id, user_id, filename, filepath, filesize, "
        "upload_size, md5, status, create_time "
        "FROM file_info "
        "WHERE user_id = ? AND filename = ? AND filesize = ? "
        "AND md5 = ? AND status = 0 "
        "ORDER BY id DESC LIMIT 1";

    MYSQL_STMT* stmt = mysql_stmt_init(conn.get());
    if (!stmt) return nullptr;

    mysql_stmt_prepare(stmt, sql, std::strlen(sql));

    unsigned long fn_len = filename.size();
    unsigned long md5_len = md5.size();

    MYSQL_BIND bind[4];
    std::memset(bind, 0, sizeof(bind));
    bind[0].buffer_type   = MYSQL_TYPE_LONGLONG;
    bind[0].buffer        = &user_id;
    bind[1].buffer_type   = MYSQL_TYPE_STRING;
    bind[1].buffer        = const_cast<char*>(filename.c_str());
    bind[1].buffer_length = fn_len;
    bind[1].length        = &fn_len;
    bind[2].buffer_type   = MYSQL_TYPE_LONGLONG;
    bind[2].buffer        = &filesize;
    bind[3].buffer_type   = MYSQL_TYPE_STRING;
    bind[3].buffer        = const_cast<char*>(md5.c_str());
    bind[3].buffer_length = md5_len;
    bind[3].length        = &md5_len;

    if (mysql_stmt_bind_param(stmt, bind) != 0 ||
        mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        return nullptr;
    }

    int64_t id=0, uid=0, fsize=0, up_size=0;
    int32_t status=0;
    char fn[256]={0}, fp[512]={0}, m5[65]={0}, ct[32]={0};
    unsigned long fn_len2, fp_len, m5_len, ct_len;

    MYSQL_BIND res[9];
    std::memset(res, 0, sizeof(res));
    res[0].buffer_type = MYSQL_TYPE_LONGLONG; res[0].buffer = &id;
    res[1].buffer_type = MYSQL_TYPE_LONGLONG; res[1].buffer = &uid;
    res[2].buffer_type = MYSQL_TYPE_STRING;   res[2].buffer = fn;  res[2].buffer_length=255; res[2].length=&fn_len2;
    res[3].buffer_type = MYSQL_TYPE_STRING;   res[3].buffer = fp;  res[3].buffer_length=511; res[3].length=&fp_len;
    res[4].buffer_type = MYSQL_TYPE_LONGLONG; res[4].buffer = &fsize;
    res[5].buffer_type = MYSQL_TYPE_LONGLONG; res[5].buffer = &up_size;
    res[6].buffer_type = MYSQL_TYPE_STRING;   res[6].buffer = m5;  res[6].buffer_length=64;  res[6].length=&m5_len;
    res[7].buffer_type = MYSQL_TYPE_LONG;     res[7].buffer = &status;
    res[8].buffer_type = MYSQL_TYPE_STRING;   res[8].buffer = ct;  res[8].buffer_length=31;  res[8].length=&ct_len;

    mysql_stmt_bind_result(stmt, res);
    mysql_stmt_store_result(stmt);

    FileInfoPtr info;
    if (mysql_stmt_fetch(stmt) == 0) {
        fn[fn_len2]='\0'; fp[fp_len]='\0'; m5[m5_len]='\0'; ct[ct_len]='\0';
        info = std::make_shared<FileInfo>();
        info->id=id; info->user_id=uid; info->filename=fn;
        info->filepath=fp; info->filesize=fsize; info->upload_size=up_size;
        info->md5=m5; info->status=status; info->create_time=ct;
    }
    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
    return info;
}

}  // namespace database
}  // namespace fileserver
