add_library(libcore STATIC
        memory.cpp
)

target_include_directories(libcore PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include
)
