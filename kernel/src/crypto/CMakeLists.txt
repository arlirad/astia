add_library(crypto STATIC
        csprng.cpp
        entropy_pool.cpp
        random.cpp
)

target_include_directories(crypto PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ASTIA_KERNEL_SOURCE_DIR}/include
)

target_link_libraries(crypto PUBLIC libcore core)
