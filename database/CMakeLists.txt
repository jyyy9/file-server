# ── database 数据库模块（预留）───────────────────────────────────
# 未来将包含: MySQL连接池, 数据访问对象
set(DATABASE_HEADERS
    database.h
)

add_library(database STATIC ${DATABASE_HEADERS})
target_include_directories(database PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(database common)