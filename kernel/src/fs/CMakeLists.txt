add_library(fs STATIC
        fs.cpp
        devfs.cpp
        initrd_fs.cpp
        initrd_init.cpp
        tmpfs.cpp
        syscalls.cpp
        init_fs.cpp
)

target_include_directories(fs PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/include
        ${CMAKE_SOURCE_DIR}/src/arch/include
        ${CMAKE_SOURCE_DIR}/src/proc/include
        ${CMAKE_SOURCE_DIR}/src/sys/include
        ${CMAKE_SOURCE_DIR}/src/initrd/include
)

target_link_libraries(fs PUBLIC libcore mm initrd)
