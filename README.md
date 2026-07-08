# FileServer

基于 muduo 主从 Reactor 模型的分布式文件管理服务。

## 项目简介

FileServer 是一个轻量级分布式文件管理服务，采用 C++11 开发，支持：

- 用户注册与认证
- 文件上传、下载、删除
- 文件元数据管理
- 大文件分块传输
- 断点续传
- 分布式服务注册与发现

## 技术栈

| 模块         | 技术                    |
| ------------ | ----------------------- |
| 开发语言     | C++11                   |
| 网络框架     | muduo                   |
| 网络模型     | 主从 Reactor 多线程模型 |
| 通信协议     | TCP                     |
| 应用层协议   | 自定义 JSON 协议        |
| 序列化       | JSON / Protobuf         |
| 数据库       | MySQL                   |
| RPC 框架     | 自研 mprpc              |
| 服务注册发现 | ZooKeeper               |

## 项目结构

```
FileServer/
├── CMakeLists.txt          # 根构建文件
├── README.md               # 项目说明
├── docs/                   # 设计文档
├── common/                 # 公共模块（日志、类型定义、宏）
├── gateway/                # 接入层网关服务
├── client/                 # 客户端 SDK
├── protocol/               # JSON 协议编解码
├── threadpool/             # 业务线程池
├── database/               # 数据库访问层
├── storage/                # 文件磁盘存储
├── services/               # 业务服务（User/File/Storage）
├── rpc/                    # RPC 通信框架
└── test/                   # 单元测试
```

## 编译 & 运行

### 前置依赖

- CMake >= 3.5
- GCC >= 4.8 或 Clang >= 3.3（支持 C++11）
- Linux 环境（pthread）

### 编译步骤

```bash
# 克隆项目
git clone <repo-url>
cd FileServer

# 构建
mkdir build
cd build
cmake ..
make

# 运行测试
./test/fileserver_test
```

### 编译模式

```bash
# Debug 模式（默认）
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Release 模式
cmake .. -DCMAKE_BUILD_TYPE=Release
```

## 架构概览

```
Client
  |
  | TCP 长连接
  |
Gateway Server (接入层)
  |
  | mprpc + ZooKeeper
  |
  +-------+-------+-------+
  |       |       |       |
User    File    Storage   ...
Service Service Service
  |       |       |
MySQL   MySQL   磁盘
```

## 当前阶段

**工程基础搭建** — 目录结构、CMake 构建系统、日志模块、公共类型定义、线程池框架就绪，各业务模块预留接口。

## License

MIT