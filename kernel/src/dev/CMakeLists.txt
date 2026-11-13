add_library(dev STATIC
        device.cpp
        blk/block_device.cpp
        device_file.cpp
        console.cpp
        driver.cpp
        mmio.cpp
        pci.cpp
        chr/tty.cpp
        chr/pseudo.cpp
        chr/pseudo_register.cpp
        chr/tty/uart16550.cpp
        nvme/nvme.cpp
        debug_exit.cpp
        init_dev.cpp
)

target_include_directories(dev PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ASTIA_KERNEL_SOURCE_DIR}/include
)

target_link_libraries(dev PUBLIC libcore arch_${ARCH} sys)
