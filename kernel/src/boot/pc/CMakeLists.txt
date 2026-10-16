add_library(boot_pc STATIC
        stub.cpp
)

target_include_directories(boot_pc PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(boot_pc PUBLIC libcore)
