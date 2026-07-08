# ── services 业务服务模块 ────────────────────────────────────────
# UserService, FileService, StorageService

set(SERVICES_SOURCES
    user_service.cc
    file_service.cc
)

set(SERVICES_HEADERS
    services.h
    user_service.h
    file_service.h
)

add_library(services STATIC ${SERVICES_SOURCES} ${SERVICES_HEADERS})

target_include_directories(services PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(services common database storage)
