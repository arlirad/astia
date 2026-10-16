add_library(dev STATIC
        uart16550.cpp
        device.cpp
        device_file.cpp
        tty.cpp
        debug_exit.cpp
        init_dev.cpp
)

target_include_directories(dev PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/include
)

target_link_libraries(dev PUBLIC libcore arch_${ARCH} sys)
