add_library(dev STATIC
        driver.cpp
        pci.cpp
        uart16550.cpp
        device.cpp
        device_file.cpp
        tty.cpp
        debug_exit.cpp
        init_dev.cpp
)

target_include_directories(dev PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(dev PUBLIC libcore arch_${ARCH} sys)
