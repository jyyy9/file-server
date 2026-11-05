# 分布式模式（独立于旧 C++ 模式）

新增 Python 标准库实现，使用 libmysqlclient 连接 MySQL，沿用现有 24B v1 Header + JSON + Binary（CRC32）协议。旧 C++ 代码、旧表及旧数据不变。旧协议中的 filepath 不适用于新模式；新客户端仅使用逻辑标识。实现过程中各阶段测试与限制记录在本文。

第一阶段：database/migrations/001_distributed.sql 添加独立 ds_* 表；`python3 -m distributed.migrate` 在选定的 FS_DB_NAME 上按版本应用迁移。空库和已有库均使用该命令；不删除旧表。DDL 自动提交，失败可重跑未记录的版本。不要通过删除 ds_* 表回退生产数据；回退程序至旧模式，保留新表和节点目录。

分片写入依次校验授权、长度、SHA-256，循环 write（重试 EINTR，拒绝零写入），fsync 文件，原子 link 发布（不覆盖已有文件），删除临时名，fsync 目录。link 提供与不覆盖 rename 等价的原子发布；相同内容重试还会同步目录。仅 write/close 不视为持久化。以上依赖操作系统和底层文件系统兑现 fsync，不宣称验证真实断电。目录独占锁阻止同一目录双实例；目录身份中保存 node_id 和 volume，启动生成新的 instance。启动清理目录内遗留临时文件。
