# ── gateway/network 网络库 ───────────────────────────────────────
# 封装 muduo TcpServer，提供会话管理和连接管理

set(NETWORK_SOURCES
    session.cc
    tcp_connection_manager.cc
    tcp_server_wrapper.cc
)

set(NETWORK_HEADERS
    session.h
    tcp_connection_manager.h
    tcp_server_wrapper.h
)

add_library(gateway_network STATIC ${NETWORK_SOURCES} ${NETWORK_HEADERS})

target_include_directories(gateway_network PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 链接 muduo 和项目公共模块
target_link_libraries(gateway_network
    PUBLIC
    muduo_net
    muduo_base
    pthread
    common
)
