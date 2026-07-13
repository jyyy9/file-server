# ── gateway/bin 服务端可执行程序 ─────────────────────────────────

add_executable(fileserver_server server_main.cc)

target_include_directories(fileserver_server PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party
)

target_link_libraries(fileserver_server
    gateway_network
    gateway_handler
    protocol
    threadpool
    database
    storage
    services
    muduo_net
    muduo_base
    ssl
    crypto
    pthread
    common
)

# Gateway 微服务模式（通过 ZK + RPC 调用下游）
add_executable(gateway_server gateway_main.cc)

target_include_directories(gateway_server PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party
)

target_link_libraries(gateway_server
    gateway_network
    gateway_handler
    protocol
    threadpool
    rpc
    muduo_net
    muduo_base
    ssl
    crypto
    pthread
    common
)
