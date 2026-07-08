# ── storage 存储模块（预留）──────────────────────────────────────
# 未来将包含: FileManager, ChunkManager 磁盘IO
set(STORAGE_HEADERS
    storage.h
)

add_library(storage STATIC ${STORAGE_HEADERS})
target_include_directories(storage PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(storage common)