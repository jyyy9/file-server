# ── rpc 远程调用模块 ─────────────────────────────────────────────
# ZooKeeper 服务注册发现 + muduo 协议 RPC 框架

# ZooKeeper C 客户端库
find_library(ZK_LIB zookeeper_mt
    PATHS /usr/lib /usr/lib/x86_64-linux-gnu /usr/local/lib)
if(NOT ZK_LIB)
    message(WARNING "未找到 libzookeeper_mt, RPC模块编译将跳过ZK功能")
    message(WARNING "安装: sudo apt-get install libzookeeper-mt-dev")
endif()

set(RPC_SOURCES
    zk_client.cc
    rpc_server.cc
    rpc_client.cc
)

set(RPC_HEADERS
    rpc.h
    zk_client.h
    rpc_server.h
    rpc_client.h
)

add_library(rpc STATIC ${RPC_SOURCES} ${RPC_HEADERS})

target_include_directories(rpc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/third_party
)

target_link_libraries(rpc
    PUBLIC
    protocol
    threadpool
    muduo_net
    muduo_base
    common
)

if(ZK_LIB)
    target_link_libraries(rpc PUBLIC ${ZK_LIB})
endif()
