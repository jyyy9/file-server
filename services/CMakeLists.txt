# ── services 业务服务模块（预留）─────────────────────────────────
# 未来将包含: UserService, FileService, StorageService
set(SERVICES_HEADERS
    services.h
)

add_library(services STATIC ${SERVICES_HEADERS})
target_include_directories(services PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(services common database storage)