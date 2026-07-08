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

# 线程池压力测试（10000 任务）
add_executable(threadpool_stress_test threadpool_stress_test.cc)

target_link_libraries(threadpool_stress_test
    threadpool
    common
    pthread
)

# 协议模块测试（粘包/半包）
add_executable(protocol_test protocol_test.cc)

target_link_libraries(protocol_test
    protocol
    common
    pthread
)

# 用户认证测试（注册/登录/验证token）
add_executable(user_auth_test user_auth_test.cc)

target_link_libraries(user_auth_test
    database
    services
    common
    pthread
)
