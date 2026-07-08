#ifndef FILESERVER_DATABASE_DATABASE_H_
#define FILESERVER_DATABASE_DATABASE_H_

// ── 数据库模块（预留）────────────────────────────────────────────
//
// 本模块提供MySQL数据库访问能力，用于：
//   - 用户表CRUD（注册、登录、Token验证）
//   - 文件元数据表CRUD（上传进度、文件信息查询）
//
// 待实现的核心类：
//   - MysqlPool      – MySQL连接池（RAII方式借用/归还连接）
//   - UserMapper     – 用户表数据访问对象
//   - FileMapper     – 文件信息表数据访问对象

namespace fileserver {
namespace database {

// 预留，后续阶段实现

}  // namespace database
}  // namespace fileserver

#endif  // FILESERVER_DATABASE_DATABASE_H_