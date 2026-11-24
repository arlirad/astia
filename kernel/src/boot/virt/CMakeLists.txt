add_library(boot_virt STATIC
        boot.S
        pvh.cpp
        multiboot2.cpp
        ../init.cpp
        init.cpp
)

target_include_directories(boot_virt PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ASTIA_KERNEL_SOURCE_DIR}/include
)

target_link_libraries(boot_virt PUBLIC libcore mm)
