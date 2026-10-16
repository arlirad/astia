add_library(plat_virt STATIC
        stub.cpp
)

target_include_directories(plat_virt PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/include
)

target_link_libraries(plat_virt PUBLIC libcore)
