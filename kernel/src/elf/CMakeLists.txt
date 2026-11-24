add_library(elf STATIC
        elf.cpp
        loader/exec.cpp
        loader/reloc.cpp
)

target_include_directories(elf
        PUBLIC ${ASTIA_KERNEL_SOURCE_DIR}/include
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(elf PUBLIC libcore)
