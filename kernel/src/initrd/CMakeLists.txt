add_library(initrd STATIC
        initrd.cpp
)

target_include_directories(initrd PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(initrd PUBLIC libcore)
