add_library(arch_aarch64 STATIC
        # Add aarch64-specific sources here
        stub.cpp
)

target_include_directories(arch_aarch64 PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(arch_aarch64 PUBLIC
        libcore
)
