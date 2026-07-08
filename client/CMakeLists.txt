# ── client 客户端模块（预留）─────────────────────────────────────
# 未来将包含: FileClient – 文件上传/下载客户端SDK
set(CLIENT_HEADERS
    client.h
)

add_library(client STATIC ${CLIENT_HEADERS})
target_include_directories(client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(client common protocol)