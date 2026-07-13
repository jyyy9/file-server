# ── gateway 网关模块 ────────────────────────────────────────────
# 子目录:
#   network/  - 网络层（TcpServer封装、连接管理、Session）
#   handler/  - 业务处理层（IO→Task→ThreadPool）
#   bin/      - 服务端可执行程序（单体 + 微服务）

add_subdirectory(network)
add_subdirectory(handler)
add_subdirectory(bin)
