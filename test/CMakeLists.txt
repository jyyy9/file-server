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

# 文件管理服务测试（上传/下载/删除/查询）
add_executable(file_service_test file_service_test.cc)

target_link_libraries(file_service_test
    database
    storage
    services
    common
    ssl
    crypto
    pthread
)

# 压力测试（200连接/50并发上传/2GB传输）
add_executable(stress_test stress_test.cc)

target_include_directories(stress_test PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party
)

target_link_libraries(stress_test
    client
    protocol
    muduo_net
    muduo_base
    common
    ssl
    crypto
    pthread
)
