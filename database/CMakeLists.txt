# ── database 数据库模块 ──────────────────────────────────────────
# MySQL连接池 + 数据访问对象

set(DATABASE_SOURCES
    mysql_pool.cc
    user_dao.cc
)

set(DATABASE_HEADERS
    database.h
    user.h
    mysql_pool.h
    user_dao.h
)

add_library(database STATIC ${DATABASE_SOURCES} ${DATABASE_HEADERS})

target_include_directories(database PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# MySQL C 客户端库
find_library(MYSQL_LIB mysqlclient
    PATHS /usr/lib /usr/lib/x86_64-linux-gnu /usr/local/lib)

if(MYSQL_LIB)
    message(STATUS "找到 MySQL: ${MYSQL_LIB}")
    target_link_libraries(database common ${MYSQL_LIB})
else()
    message(WARNING "未找到 libmysqlclient，请安装: sudo apt-get install libmysqlclient-dev")
    target_link_libraries(database common mysqlclient)
endif()
