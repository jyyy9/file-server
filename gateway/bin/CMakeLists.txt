# ── gateway/bin 服务端可执行程序 ─────────────────────────────────

add_executable(fileserver_server server_main.cc)

target_link_libraries(fileserver_server
    gateway_network
    muduo_net
    muduo_base
    pthread
    common
)
