add_library(plat_pc STATIC
        ../virt/fwcfg.cpp
        ../virt/acpi_tables.cpp
)

target_include_directories(plat_pc PUBLIC
        ${ASTIA_KERNEL_SOURCE_DIR}/include
        ${ASTIA_KERNEL_SOURCE_DIR}/src/arch/x86_64
        ${ASTIA_KERNEL_SOURCE_DIR}/src/plat/virt
)

target_link_libraries(plat_pc PUBLIC libcore)
