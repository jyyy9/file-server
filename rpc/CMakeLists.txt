# ── rpc 远程调用模块（预留）──────────────────────────────────────
# 未来将包含: mprpc框架，服务间通信
set(RPC_HEADERS
    rpc.h
)

add_library(rpc STATIC ${RPC_HEADERS})
target_include_directories(rpc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rpc common protocol)