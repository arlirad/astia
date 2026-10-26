add_library(dev STATIC
        device.cpp
        device_file.cpp
        driver.cpp
        pci.cpp
        chr/tty.cpp
        chr/pseudo.cpp
        chr/pseudo_register.cpp
        chr/tty/uart16550.cpp
        debug_exit.cpp
        init_dev.cpp
)

target_include_directories(dev PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(dev PUBLIC libcore arch_${ARCH} sys)
