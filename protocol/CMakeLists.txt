# ── protocol 协议模块 ────────────────────────────────────────────
# 基于二进制头 + JSON Body + Binary Data 的自定义应用层协议
# 支持 TCP 粘包/半包处理

set(PROTOCOL_SOURCES
    message.cc
    encoder.cc
    decoder.cc
)

set(PROTOCOL_HEADERS
    protocol.h
    message_header.h
    message.h
    encoder.h
    decoder.h
)

add_library(protocol STATIC ${PROTOCOL_SOURCES} ${PROTOCOL_HEADERS})

target_include_directories(protocol PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/third_party
)

target_link_libraries(protocol common)
