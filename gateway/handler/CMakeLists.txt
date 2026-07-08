# ── gateway/handler 业务处理层 ───────────────────────────────────
# 连接 IO 线程和 Worker 线程的桥梁

set(HANDLER_SOURCES
    business_processor.cc
)

set(HANDLER_HEADERS
    task.h
    business_processor.h
)

add_library(gateway_handler STATIC ${HANDLER_SOURCES} ${HANDLER_HEADERS})

target_include_directories(gateway_handler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(gateway_handler
    PUBLIC
    threadpool
    protocol
    muduo_net
    muduo_base
    common
)
