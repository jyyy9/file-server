# ── services 业务服务静态库 ──────────────────────────────────────

set(SERVICES_SOURCES
    user_service.cc
    file_service.cc
)

set(SERVICES_HEADERS
    user_service.h
    file_service.h
)

add_library(services STATIC ${SERVICES_SOURCES} ${SERVICES_HEADERS})
target_include_directories(services PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(services common database storage)

# ── 微服务可执行程序 ────────────────────────────────────────────

# UserService 微服务 (RPC:9001)
add_executable(user_service_server user_service_server.cc)
target_link_libraries(user_service_server
    rpc database services common pthread)

# FileService 微服务 (RPC:9002)
add_executable(file_service_server file_service_server.cc)
target_link_libraries(file_service_server
    rpc database storage services common pthread)

# StorageService 微服务 (RPC:9003)
add_executable(storage_service_server storage_service_server.cc)
target_link_libraries(storage_service_server
    rpc storage common pthread)
