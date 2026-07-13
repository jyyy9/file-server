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
│   └── file_info.h + file_dao.h/.cc  # 文件元数据实体 + DAO (含断点续传查询)
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
| muduo | 网络库 (TcpServer/EventLoop/TcpClient) | 克隆到 third_party/muduo, ./build.sh install |
| MySQL | 数据库 | sudo apt install libmysqlclient-dev |
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

## 运行

### 单体模式 (单进程)

```bash
# 1. 确保 MySQL 运行 + 建库
mysql -u root -p -e "CREATE DATABASE IF NOT EXISTS fileserver"

# 2. 启动服务端
./gateway/bin/fileserver_server &

# 3. 启动客户端
./client/fileserver_client
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
