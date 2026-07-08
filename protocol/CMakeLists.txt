# ── protocol 协议模块（预留）────────────────────────────────────
# 未来将包含: JsonCodec, Message, 长度前缀帧解析
set(PROTOCOL_HEADERS
    protocol.h
)

add_library(protocol STATIC ${PROTOCOL_HEADERS})
target_include_directories(protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(protocol common)