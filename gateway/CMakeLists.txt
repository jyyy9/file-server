# ── gateway 网关模块（预留）──────────────────────────────────────
# 未来将包含: TcpServer, ConnectionManager, 请求路由器
set(GATEWAY_HEADERS
    gateway.h
)

add_library(gateway STATIC ${GATEWAY_HEADERS})
target_include_directories(gateway PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gateway common protocol)