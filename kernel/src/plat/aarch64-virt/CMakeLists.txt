add_library(plat_aarch64_virt STATIC
        pl011.cpp
        psci.cpp
)

target_include_directories(plat_aarch64_virt PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ASTIA_KERNEL_SOURCE_DIR}/include
)
