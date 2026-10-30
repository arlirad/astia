add_library(dbg STATIC
        stacktrace.cpp
)

target_include_directories(dbg PUBLIC
        ${ASTIA_KERNEL_SOURCE_DIR}/include
)
