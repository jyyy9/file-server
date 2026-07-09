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
