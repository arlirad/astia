add_library(hw STATIC
        ../hw_pci.cpp
        ../dev/mmio.cpp
)

target_include_directories(hw PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ASTIA_KERNEL_SOURCE_DIR}/include
)

target_link_libraries(hw PUBLIC libcore mm arch_${ARCH})
