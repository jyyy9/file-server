# ── client 客户端模块 ────────────────────────────────────────────
# 交互式命令行客户端

set(CLIENT_SOURCES
    file_client.cc
    console_ui.cc
)

set(CLIENT_HEADERS
    client.h
    file_client.h
    console_ui.h
)

add_library(client STATIC ${CLIENT_SOURCES} ${CLIENT_HEADERS})

target_include_directories(client PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/third_party
)

target_link_libraries(client
    protocol
    muduo_net
    muduo_base
    common
    ssl
    crypto
    pthread
)

# 客户端可执行程序
add_executable(fileserver_client main.cpp)

target_link_libraries(fileserver_client
    client
    protocol
    muduo_net
    muduo_base
    common
    ssl
    crypto
    pthread
)
