-- ============================================================================
-- FileServer 数据库初始化脚本
-- ----------------------------------------------------------------------------
-- 适用版本: MySQL 5.7+ / 8.0
-- 用法:     mysql -h 127.0.0.1 -u root -p < database/schema.sql
-- 说明:     脚本可重复执行（CREATE ... IF NOT EXISTS），不会覆盖已有数据
-- ============================================================================

CREATE DATABASE IF NOT EXISTS `fileserver`
    DEFAULT CHARACTER SET utf8mb4
    DEFAULT COLLATE utf8mb4_unicode_ci;

USE `fileserver`;

SET NAMES utf8mb4;

-- ----------------------------------------------------------------------------
-- 表: user —— 用户账号表
-- 对应代码: database/user_dao.cc, services/user_service.cc
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `user` (
    `id`          INT          NOT NULL AUTO_INCREMENT COMMENT '用户ID',
    `username`    VARCHAR(64)  NOT NULL                COMMENT '用户名，全局唯一',
    `password`    VARCHAR(128) NOT NULL                COMMENT '密码（MD5 摘要，64 位十六进制）',
    `token`       VARCHAR(128) NOT NULL DEFAULT ''     COMMENT '登录令牌，未登录为空串',
    `create_time` DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '注册时间',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_username` (`username`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='用户账号表';

-- ----------------------------------------------------------------------------
-- 表: file_info —— 文件元数据表
-- 对应代码: database/file_dao.cc, services/file_service.cc
--
-- status 取值（见 services/file_service.cc）:
--   0 = 上传中/正常
--   1 = 上传完成
--   2 = 已删除（逻辑删除，查询时过滤 status != 2）
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `file_info` (
    `id`          INT          NOT NULL AUTO_INCREMENT COMMENT '文件ID',
    `user_id`     INT          NOT NULL                COMMENT '所属用户ID（user.id）',
    `filename`    VARCHAR(255) NOT NULL                COMMENT '文件名',
    `filepath`    VARCHAR(512) NOT NULL DEFAULT ''     COMMENT '服务器存储路径',
    `filesize`    BIGINT       NOT NULL DEFAULT 0      COMMENT '文件总大小（字节）',
    `upload_size` BIGINT       NOT NULL DEFAULT 0      COMMENT '已上传大小（字节），断点续传用',
    `md5`         VARCHAR(64)  NOT NULL DEFAULT ''     COMMENT '文件 MD5 摘要',
    `status`      INT          NOT NULL DEFAULT 0      COMMENT '状态: 0上传中 1完成 2已删除',
    `create_time` DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    PRIMARY KEY (`id`),
    KEY `idx_user_id` (`user_id`),
    KEY `idx_user_status` (`user_id`, `status`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='文件元数据表';
