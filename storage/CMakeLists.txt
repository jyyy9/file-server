# ── storage 存储模块 ─────────────────────────────────────────────
# 磁盘文件读写（chunk 级别，不一次性加载整个文件到内存）

set(STORAGE_SOURCES
    storage_manager.cc
)

set(STORAGE_HEADERS
    storage_manager.h
)

add_library(storage STATIC ${STORAGE_SOURCES} ${STORAGE_HEADERS})
target_include_directories(storage PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(storage common)
