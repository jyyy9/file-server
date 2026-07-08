# ── test 测试程序 ───────────────────────────────────────────────
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