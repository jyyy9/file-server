cmake_minimum_required(VERSION 3.5)
project(FileServer VERSION 1.0.0 LANGUAGES CXX)

# ── C++11 标准 ──────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── 编译选项 ─────────────────────────────────────────────────────
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING
        "选择编译类型: Debug 或 Release" FORCE)
endif()

message(STATUS "FileServer 编译类型: ${CMAKE_BUILD_TYPE}")

# ── Debug / Release 编译标志 ─────────────────────────────────────
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0 -Wall -Wextra -Wpedantic)
    message(STATUS "Debug 模式: -g -O0 -Wall -Wextra")
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O2 -DNDEBUG)
    message(STATUS "Release 模式: -O2 -DNDEBUG")
endif()

# ── 链接选项 ─────────────────────────────────────────────────────
if(UNIX)
    add_link_options(-pthread)
endif()

# ── 头文件搜索路径 ───────────────────────────────────────────────
include_directories(${PROJECT_SOURCE_DIR})

# ── 子目录 ───────────────────────────────────────────────────────
add_subdirectory(common)
add_subdirectory(threadpool)
add_subdirectory(protocol)
add_subdirectory(gateway)
add_subdirectory(client)
add_subdirectory(database)
add_subdirectory(storage)
add_subdirectory(services)
add_subdirectory(rpc)
add_subdirectory(test)