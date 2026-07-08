# ── gateway 网关模块 ────────────────────────────────────────────
# 子目录:
#   network/  - 网络层（TcpServer封装、连接管理、Session）
#   handler/  - 业务处理层（IO→Task→ThreadPool）
#   bin/      - 服务端可执行程序

add_subdirectory(network)
add_subdirectory(handler)
add_subdirectory(bin)

# 保留旧的 header-only 库作为占位（后续业务层代码放这里面）
add_library(gateway STATIC gateway.h)
target_include_directories(gateway PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gateway common protocol gateway_network)
