add_library(fs STATIC
        fs.cpp
        devfs.cpp
        initrd_fs.cpp
        initrd_init.cpp
        tmpfs.cpp
        syscalls.cpp
        syscalls/io_setup.cpp
        syscalls/io_destroy.cpp
        syscalls/io_enter.cpp
        syscalls/io_register.cpp
        syscalls/mount.cpp
        syscalls/unmount.cpp
        init_fs.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../ipc/pipe.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../ipc/unix_socket.cpp
)

target_include_directories(fs PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/arch
        ${CMAKE_SOURCE_DIR}/src/proc
        ${CMAKE_SOURCE_DIR}/src/sys
        ${CMAKE_SOURCE_DIR}/src/initrd
)

target_link_libraries(fs PUBLIC libcore mm initrd ipc)
