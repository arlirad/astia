add_library(dbg STATIC
        panic_report.cpp
)

if (ARCH STREQUAL "x86_64")
    target_sources(dbg PRIVATE stacktrace.cpp)
endif ()

target_include_directories(dbg PUBLIC
        ${ASTIA_KERNEL_SOURCE_DIR}/include
)
