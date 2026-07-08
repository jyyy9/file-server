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

# ── muduo 网络库 ──────────────────────────────────────────────────
# muduo 是 Linux 专用网络库，需提前安装。
# 使用方式: cmake .. -DMUDUO_INSTALL_DIR=~/muduo/build/release-install-cpp11
# 安装步骤:
#   git clone https://github.com/chenshuo/muduo.git
#   cd muduo && ./build.sh install
if(UNIX)
    set(MUDUO_INSTALL_DIR "" CACHE PATH "muduo 安装路径 (例如: ~/muduo/build/release-install-cpp11)")

    if(MUDUO_INSTALL_DIR)
        # 用户指定了 muduo 安装路径
        set(MUDUO_INCLUDE_DIR ${MUDUO_INSTALL_DIR}/include)
        set(MUDUO_LIB_DIR     ${MUDUO_INSTALL_DIR}/lib)

        if(EXISTS ${MUDUO_INCLUDE_DIR} AND EXISTS ${MUDUO_LIB_DIR})
            include_directories(${MUDUO_INCLUDE_DIR})
            link_directories(${MUDUO_LIB_DIR})
            message(STATUS "muduo 路径: ${MUDUO_INSTALL_DIR}")
        else()
            message(WARNING "muduo 路径无效，请检查 -DMUDUO_INSTALL_DIR=${MUDUO_INSTALL_DIR}")
        endif()
    else()
        # 未指定路径: 尝试自动查找
        message(STATUS "未指定 MUDUO_INSTALL_DIR，尝试自动查找 muduo...")
        find_path(MUDUO_INCLUDE_DIR muduo/net/TcpServer.h
            PATHS /usr/include /usr/local/include
                  $ENV{HOME}/muduo/build/release-install-cpp11/include)
        find_library(MUDUO_NET_LIB muduo_net
            PATHS /usr/lib /usr/local/lib
                  $ENV{HOME}/muduo/build/release-install-cpp11/lib)
        if(MUDUO_INCLUDE_DIR AND MUDUO_NET_LIB)
            include_directories(${MUDUO_INCLUDE_DIR})
            get_filename_component(MUDUO_LIB_DIR ${MUDUO_NET_LIB} DIRECTORY)
            link_directories(${MUDUO_LIB_DIR})
            message(STATUS "找到 muduo: include=${MUDUO_INCLUDE_DIR}, lib=${MUDUO_LIB_DIR}")
        else()
            message(WARNING "未找到 muduo！请通过 -DMUDUO_INSTALL_DIR=<path> 指定安装路径")
            message(WARNING "安装: git clone https://github.com/chenshuo/muduo.git && cd muduo && ./build.sh install")
        endif()
    endif()
endif()

# ── 链接选项 ─────────────────────────────────────────────────────
# 为所有目标添加 -pthread 编译/链接选项，以支持多线程编程,设置为全局的，不用每个子目录都进行添加。
# 自动链接！ 只要你在 CMake 中加了 -pthread 选项，不管代码里用没用到多线程，链接器都会去链接 libpthread.so
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