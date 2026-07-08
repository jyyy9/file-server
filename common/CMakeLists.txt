# ── common 公共库 ───────────────────────────────────────────────
set(COMMON_SOURCES
    logger.cc
)

set(COMMON_HEADERS
    logger.h
    common.h
)

add_library(common STATIC ${COMMON_SOURCES} ${COMMON_HEADERS})

target_include_directories(common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})