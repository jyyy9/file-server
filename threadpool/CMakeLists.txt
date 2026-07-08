# ── threadpool 线程池库 ─────────────────────────────────────────
set(THREADPOOL_SOURCES
    thread_pool.cc
)

set(THREADPOOL_HEADERS
    thread_pool.h
)

add_library(threadpool STATIC ${THREADPOOL_SOURCES} ${THREADPOOL_HEADERS})

target_include_directories(threadpool PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(threadpool common)