# ── test 测试程序 ───────────────────────────────────────────────

# 单元测试
add_executable(fileserver_test main.cpp)
target_link_libraries(fileserver_test
    common threadpool protocol client database storage services rpc gateway_network
)

# 线程池压力测试（10000 任务）
add_executable(threadpool_stress_test threadpool_stress_test.cc)
target_link_libraries(threadpool_stress_test threadpool common pthread)

# 协议模块测试（粘包/半包/批量）
add_executable(protocol_test protocol_test.cc)
target_link_libraries(protocol_test protocol common pthread)

# 用户认证测试（注册/登录/验证token）
add_executable(user_auth_test user_auth_test.cc)
target_link_libraries(user_auth_test database services common pthread)

# 文件管理服务测试（上传/下载/删除/查询/断点续传/MD5校验）
add_executable(file_service_test file_service_test.cc)
target_link_libraries(file_service_test database storage services common ssl crypto pthread)

# 端到端集成测试（4个场景: 全链路/并发/断点续传/错误处理）
add_executable(integration_test integration_test.cc)
target_include_directories(integration_test PRIVATE ${PROJECT_SOURCE_DIR}/third_party)
target_link_libraries(integration_test client protocol muduo_net muduo_base common ssl crypto pthread)
