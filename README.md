# FileServer

基于 muduo 主从 Reactor 模型的分布式文件管理服务，C++11 实现。

---

## 项目架构

```
┌─────────────────────────────────────────────────────┐
│                      Client                         │
│           (交互式命令行 / FileClient SDK)            │
└──────────────────────┬──────────────────────────────┘
                       │ 自定义二进制协议 (Header+JSON+Binary)
                       ▼
┌─────────────────────────────────────────────────────┐
│                    Gateway                          │
│   muduo TcpServer (4 Sub Reactor)                  │
│   ├── Decoder (粘包/半包)                           │
│   ├── BusinessProcessor (IO→Task→ThreadPool)       │
│   └── RequestRouter (cmd→handler)                  │
└──┬──────────────┬──────────────┬───────────────────┘
   │              │              │
   ▼              ▼              ▼
┌─────────┐  ┌─────────┐  ┌──────────────┐
│  User   │  │  File   │  │   Storage    │
│ Service │  │ Service │  │   Service    │
└────┬────┘  └────┬────┘  └──────┬───────┘
     │            │              │
     ▼            ▼              ▼
   MySQL       MySQL          Disk
```

---

## 目录结构 (7,887 行 C++)

```
FileServer/
├── CMakeLists.txt                    # 根构建 (C++11, Debug/Release, muduo/MySQL/ZK查找)
├── README.md
│
├── common/          244 行           # 公共模块
│   ├── common.h                      # 错误码、文件状态枚举、工具宏、常量
│   └── logger.h/.cc                  # 线程安全日志单例 (级别过滤/文件输出)
│
├── threadpool/      147 行           # 线程池
│   └── thread_pool.h/.cc             # 固定线程池 + Submit(future) + mutex+cond_var
│
├── protocol/        554 行           # 应用层协议
│   ├── message_header.h              # 24B 二进制头 (魔数/版本/长度/CRC32/字节序转换)
│   ├── message.h/.cc                 # Message (header + json_body + binary_data)
│   ├── encoder.h/.cc                 # 序列化 (CRC32校验)
│   └── decoder.h/.cc                 # 反序列化状态机 (粘包/半包处理)
│
├── gateway/       1,081 行           # 网关层
│   ├── network/
│   │   ├── session.h/.cc             # 会话 (weak_ptr持有TcpConnection)
│   │   ├── tcp_connection_manager.h/.cc  # 连接管理器 (线程安全)
│   │   └── tcp_server_wrapper.h/.cc      # muduo TcpServer封装 (主从Reactor)
│   ├── handler/
│   │   ├── task.h                    # 业务任务结构体
│   │   ├── business_processor.h/.cc  # IO→Task→Pool 桥梁
│   │   └── request_router.h/.cc      # cmd → handler 路由
│   └── bin/
│       ├── server_main.cc            # 单体服务端入口
│       └── gateway_main.cc           # 微服务Gateway入口 (ZK+RPC)
│
├── database/      1,060 行           # 数据库层
│   ├── mysql_pool.h/.cc              # MySQL连接池 (RAII, shared_ptr自定义删除器)
│   ├── user.h + user_dao.h/.cc       # 用户实体 + 数据访问 (Prepared Statement)
│   ├── file_info.h + file_dao.h/.cc  # 文件元数据实体 + DAO (含断点续传查询)
│   └── schema.sql                    # 建库建表 SQL (数据库初始化)
│
├── storage/        247 行            # 存储层
│   └── storage_manager.h/.cc         # 分块磁盘读写 (4MB chunk, seekp/seekg, 按user_id分目录)
│
├── services/       573 行            # 业务服务层
│   ├── user_service.h/.cc            # 注册/登录/Token验证
│   ├── file_service.h/.cc            # 上传/下载/删除/查询/断点续传
│   ├── user_service_server.cc        # UserService 微服务进程 (:9001)
│   ├── file_service_server.cc        # FileService 微服务进程 (:9002)
│   └── storage_service_server.cc     # StorageService 微服务进程 (:9003)
│
├── rpc/            520 行            # RPC 框架
│   ├── zk_client.h/.cc               # ZooKeeper 服务注册/发现
│   ├── rpc_server.h/.cc              # RPC服务端 (muduo+协议+ThreadPool)
│   └── rpc_client.h/.cc              # RPC客户端 (promise/future同步调用)
│
├── client/         784 行            # 客户端
│   ├── file_client.h/.cc             # 核心 (muduo TcpClient + Encoder/Decoder + promise/future)
│   ├── console_ui.h/.cc              # 终端菜单 + 进度条
│   └── main.cpp                      # 入口
│
└── test/          1,709 行           # 测试
    ├── main.cpp                      # 单元测试 (日志/线程池/公共类型)
    ├── protocol_test.cc              # 协议测试 (单消息/粘包/半包)
    ├── threadpool_stress_test.cc     # 线程池压力 (10000任务)
    ├── user_auth_test.cc             # 用户认证测试 (注册/登录/Token)
    ├── file_service_test.cc          # 文件服务测试 (上传/下载/MD5/断点续传)
    └── integration_test.cc           # 端到端集成测试 (4场景)
```

---

## 协议格式

```
+----------------------+
| MessageHeader (24B)  |  magic(2) + version(2) + msgType(4) + bodyLen(4)
|                      |  + dataLen(4) + requestId(4) + checksum(4)
+----------------------+
| JSON Body            |  可变长, 如 {"cmd":"login","username":"admin"}
+----------------------+
| Binary Data          |  可变长, 文件 chunk 数据
+----------------------+
```

---

## 依赖

| 依赖 | 用途 | 安装 |
|------|------|------|
| CMake / g++ / make | 构建工具 | sudo apt install cmake build-essential |
| muduo | 网络库 (TcpServer/EventLoop/TcpClient) | 克隆到 third_party/muduo, ./build.sh install |
| Boost | muduo 编译依赖 (仅头文件) | sudo apt install libboost-dev |
| MySQL | 数据库 (服务端 + 客户端 + 开发库) | sudo apt install mysql-server mysql-client libmysqlclient-dev |
| nlohmann/json | JSON解析 | 下载 json.hpp 到 third_party/nlohmann/ |
| OpenSSL | MD5校验 | sudo apt install libssl-dev |
| ZooKeeper | 服务注册发现 (可选, 微服务模式) | sudo apt install libzookeeper-mt-dev zookeeper |
| pthread | 多线程 | 系统自带 |

---

## 编译

```bash
# 1. 安装 muduo
mkdir -p third_party && cd third_party
git clone https://github.com/chenshuo/muduo.git
cd muduo && ./build.sh install && cd ../..

# 2. 下载 nlohmann/json
mkdir -p third_party/nlohmann && cd third_party/nlohmann
wget --no-check-certificate https://github.com/nlohmann/json/releases/latest/download/json.hpp
cd ../..

# 3. 编译
mkdir build && cd build
cmake ..
make
```

---

## 数据库初始化

首次运行前必须先建库建表。否则服务端能正常启动，但注册、上传等操作会失败，日志报
`Table 'fileserver.user' doesn't exist`。

```bash
# 1. 启动 MySQL (WSL 未启用 systemd 时用 service 而非 systemctl)
sudo service mysql start

# 2. 建库 + 建表 (脚本可重复执行，不会覆盖已有数据)
mysql -h 127.0.0.1 -u root -p < database/schema.sql

# 3. 验证
mysql -h 127.0.0.1 -u root -p -D fileserver -e "SHOW TABLES;"
```

表结构定义见 [database/schema.sql](database/schema.sql)：

| 表 | 说明 |
|---|---|
| `user` | 用户账号、密码摘要（MD5）、登录 token |
| `file_info` | 文件元数据、上传进度（断点续传） |

服务端（单体模式与三个微服务）的连接参数均支持环境变量覆盖，未设置时使用默认值：

| 环境变量 | 默认值 | 说明 | 使用者 |
|---|---|---|---|
| `FS_DB_HOST` | `127.0.0.1` | 数据库地址 | 单体 / 微服务 |
| `FS_DB_PORT` | `3306` | 数据库端口 | 单体 / 微服务 |
| `FS_DB_USER` | `root` | 数据库用户名 | 单体 / 微服务 |
| `FS_DB_PASS` | `123456` | 数据库密码 | 单体 / 微服务 |
| `FS_DB_NAME` | `fileserver` | 库名 | 单体 / 微服务 |
| `FS_STORAGE_ROOT` | `./storage/data` | 文件落盘目录 | 单体 / file / storage |
| `FS_ZK_HOSTS` | `127.0.0.1:2181` | ZooKeeper 地址 | 微服务 / gateway |
| `FS_ADVERTISE_HOST` | `127.0.0.1` | 注册到 ZooKeeper 的服务地址（多机/容器部署时设为本机可达 IP 或容器名） | 微服务 |

读取顺序是 **环境变量 → 默认值**：设置了环境变量就用它，否则才用上表的默认值。
默认值只服务于本地开发；部署到服务器时请使用自己的凭据，**不要修改代码**。

```bash
# 部署前：清掉遗留配置，再设置自己的（变量名必须与上表完全一致）
unset FS_DB_HOST FS_DB_USER FS_DB_PASS FS_DB_NAME FS_STORAGE_ROOT

export FS_DB_HOST=127.0.0.1
export FS_DB_USER=root
export FS_DB_PASS='你的数据库密码'
export FS_DB_NAME=fileserver
export FS_STORAGE_ROOT=/var/lib/fileserver/data

# 环境变量只在进程启动时读取一次，修改后必须重启服务端才生效
./build/gateway/bin/fileserver_server

# 检查当前生效的配置
env | grep FS_
```

注意：

- 把变量设成**空字符串**（`export FS_DB_PASS=`）不会回退到默认值，程序会拿空值去连库；
  想恢复默认请用 `unset`
- 密码不要写进 README、源码或 Git；建议放在不提交的启动脚本里，或 systemd 的 `Environment=` 配置中

> MySQL 8 默认给 root 使用 `auth_socket` 认证，若 `mysql -u root -p` 无法登录，
> 先执行 `sudo mysql` 进入，再执行：
> `ALTER USER 'root'@'localhost' IDENTIFIED WITH mysql_native_password BY '123456';`

---

## 运行

### 单体模式 (单进程)

```bash
# 1. 确保 MySQL 已启动，且已完成「数据库初始化」一节

# 2. 启动服务端
./gateway/bin/fileserver_server &

# 3. 启动客户端
./client/fileserver_client
```

客户端默认连接 `127.0.0.1:8080`。连接远程服务端时，按顺序传入地址和端口：

```bash
./client/fileserver_client <服务器IP> <端口>
```

### 微服务模式 (4进程, 需 ZooKeeper)

```bash
# 启动 ZK
sudo /usr/share/zookeeper/bin/zkServer.sh start

# 启动微服务
./services/user_service_server &
./services/file_service_server &
./services/storage_service_server &
./gateway/bin/gateway_server &

# 客户端照常
./client/fileserver_client
```

---

## 测试

```bash
./test/fileserver_test          # 单元测试
./test/protocol_test            # 协议粘包/半包
./test/threadpool_stress_test   # 线程池10000任务
./test/user_auth_test           # 用户认证 (需MySQL)
./test/file_service_test        # 文件上传/下载/断点续传 (需MySQL)
./test/integration_test         # 端到端集成测试 (需服务端运行)
```

---

## 技术栈

| 层次 | 技术 |
|------|------|
| 语言 | C++11 |
| 网络模型 | muduo 主从 Reactor (1 Main + 4 Sub) |
| 传输层 | TCP |
| 应用层协议 | 自定义二进制协议 (24B头 + JSON + Binary) |
| 序列化 | JSON (nlohmann) + 二进制 |
| 数据库 | MySQL (连接池 RAII + Prepared Statement) |
| 并发 | ThreadPool (mutex + condition_variable) |
| 存储 | 分块读写 (4MB chunk, 流式, 不整文件加载) |
| 断点续传 | MD5匹配 + upload_size偏移量 |
| RPC | 自研 mprpc (muduo + 二进制协议 + promise/future) |
| 服务发现 | ZooKeeper (临时顺序节点) |
| 内存管理 | shared_ptr/weak_ptr + RAII |

---

## 设计原则

- **IO线程不做耗时操作**: Sub Reactor 只负责编解码+网络收发, 业务提交到 ThreadPool
- **Service层不写SQL**: 通过 DAO 访问数据库
- **禁止整文件加载到内存**: 按 4MB chunk 流式读写
- **控制面与数据面分离**: 元数据走 RPC, 大文件 chunk 走直连 TCP
- **路径规则**: `{root}/{user_id}/{YYYYMMDD}_{HHmmss}_{sanitized_name}`

## 新增：分布式对象存储原型

独立模式位于 `distributed/`，保留原 C++ 单体及微服务模式、旧表和数据。提供 FileService + MySQL、三节点双副本、不可变分片、续传、校验下载和后台任务。新模式使用 Python 标准库及系统 libmysqlclient，继续使用现有 Header + JSON + Binary 协议。

部署、迁移、接口、状态机、演示和已验证/未验证范围见 [分布式模式文档](docs/distributed.md)。主要验收入口为 `scripts/test-distributed.sh`，必须使用独立空的 `ds_test_*` 数据库。当前尚未完成全部故障与旧模式数据库回归验收，详见验证记录。
