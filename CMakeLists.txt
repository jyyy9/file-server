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
    add_compile_options(-g -O0 -Wall -Wextra)
    message(STATUS "Debug 模式: -g -O0 -Wall -Wextra")
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O2 -DNDEBUG)
    message(STATUS "Release 模式: -O2 -DNDEBUG")
endif()

# ── muduo 网络库 ──────────────────────────────────────────────────
# muduo 编译产物默认在 ../build/release-install-cpp11/（相对于 muduo 源码目录）
# 即: third_party/build/release-install-cpp11/
#
# 安装步骤:
#   mkdir -p third_party && cd third_party
#   git clone https://github.com/chenshuo/muduo.git
#   cd muduo && ./build.sh install
#
# CMake 自动查找优先级:
#   1. ${PROJECT_SOURCE_DIR}/third_party/build/release-install-cpp11  (项目自带)
#   2. /usr/local /usr                                                (系统安装)
#   3. ~/muduo/build/release-install-cpp11                            (家目录)
#   4. -DMUDUO_INSTALL_DIR=<path> 手动指定
if(UNIX)
    set(MUDUO_DEFAULT_DIR ${PROJECT_SOURCE_DIR}/third_party/build/release-install-cpp11)
    set(MUDUO_INSTALL_DIR "" CACHE PATH "muduo 安装路径（留空则自动查找）")

    if(MUDUO_INSTALL_DIR)
        set(MUDUO_SEARCH_DIRS ${MUDUO_INSTALL_DIR})
    else()
        set(MUDUO_SEARCH_DIRS
            ${MUDUO_DEFAULT_DIR}
            /usr/local
            /usr
            $ENV{HOME}/muduo/build/release-install-cpp11)
    endif()

    find_path(MUDUO_INCLUDE_DIR muduo/net/TcpServer.h
        PATHS ${MUDUO_SEARCH_DIRS}
        PATH_SUFFIXES include)

    find_library(MUDUO_NET_LIB muduo_net
        PATHS ${MUDUO_SEARCH_DIRS}
        PATH_SUFFIXES lib)

    if(MUDUO_INCLUDE_DIR AND MUDUO_NET_LIB)
        include_directories(${MUDUO_INCLUDE_DIR})
        get_filename_component(MUDUO_LIB_DIR ${MUDUO_NET_LIB} DIRECTORY)
        link_directories(${MUDUO_LIB_DIR})
        message(STATUS "找到 muduo: ${MUDUO_INCLUDE_DIR}")
    else()
        message(FATAL_ERROR
            "未找到 muduo 库！请先安装:\n"
            "  mkdir -p third_party && cd third_party\n"
            "  git clone https://github.com/chenshuo/muduo.git\n"
            "  cd muduo && ./build.sh install\n"
            "  cd ../.. && rm -rf build && mkdir build && cd build && cmake .. && make\n"
            "或指定自定义路径: cmake .. -DMUDUO_INSTALL_DIR=/path/to/muduo/build/release-install-cpp11")
    endif()
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
