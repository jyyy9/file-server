# ── test 测试程序 ───────────────────────────────────────────────

# 单元测试（已有）
set(TEST_SOURCES
    main.cpp
)

add_executable(fileserver_test ${TEST_SOURCES})

target_link_libraries(fileserver_test
    common
    threadpool
    protocol
    gateway
    client
    database
    storage
    services
    rpc
)

# 客户端并发测试程序（100个连接）
add_executable(client_test client_test.cc)

target_link_libraries(client_test
    pthread
)
