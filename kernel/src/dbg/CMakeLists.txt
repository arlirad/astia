add_library(dbg STATIC
        stacktrace.cpp
)

target_include_directories(dbg PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)
