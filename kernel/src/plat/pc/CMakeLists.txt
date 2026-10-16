add_library(plat_pc STATIC
        stub.cpp
)

target_include_directories(plat_pc PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(plat_pc PUBLIC libcore)
