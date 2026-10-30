add_library(libcore STATIC
        memory.cpp
        cxxabi.cpp
)

target_include_directories(libcore PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ASTIA_KERNEL_SOURCE_DIR}/include
)
