add_library(elf STATIC
        elf.cpp
        exec_loader.cpp
)

target_include_directories(elf PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ASTIA_KERNEL_SOURCE_DIR}/include
)

target_link_libraries(elf PUBLIC libcore)
