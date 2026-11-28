add_library(plat_virt STATIC
        fwcfg.cpp
        acpi_tables.cpp
)

target_include_directories(plat_virt PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ASTIA_KERNEL_SOURCE_DIR}/include
        ${ASTIA_KERNEL_SOURCE_DIR}/src/arch/x86_64
)

target_link_libraries(plat_virt PUBLIC libcore)
