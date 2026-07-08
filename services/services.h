#ifndef FILESERVER_SERVICES_SERVICES_H_
#define FILESERVER_SERVICES_SERVICES_H_

// ── 业务服务模块（预留）──────────────────────────────────────────
//
// 本模块实现核心业务逻辑服务：
//
//   UserService（用户服务）:
//     - Register(username, password) -> token     注册
//     - Login(username, password)    -> token     登录
//     - VerifyToken(token)           -> user_id   验证令牌
//     - Logout(token)                             登出
//
//   FileService（文件服务）:
//     - UploadStart(user_id, filename, filesize, md5) -> file_id, offset
//                                                  初始化上传
//     - UploadData(file_id, offset, data)          上传数据块
//     - DownloadStart(file_id) -> file_info        开始下载
//     - Delete(user_id, file_id)                   删除文件
//     - Query(user_id) -> file list                查询文件列表
//
//   StorageService（存储服务）:
//     - WriteChunk(file_id, offset, data)          写入分块
//     - ReadChunk(file_id, offset, size) -> data   读取分块
//     - MergeFile(file_id)                         合并分块
//     - DeleteFile(file_id)                        删除文件

namespace fileserver {
namespace services {

// 预留，后续阶段实现

}  // namespace services
}  // namespace fileserver

#endif  // FILESERVER_SERVICES_SERVICES_H_