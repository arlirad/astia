file(GLOB ASTIA_FS_AIO_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/aio/*.cpp)
file(GLOB_RECURSE ASTIA_FS_SYSCALL_SOURCES CONFIGURE_DEPENDS syscalls/*.cpp)

add_library(fs STATIC
        ${ASTIA_FS_AIO_SOURCES}
        ${ASTIA_FS_SYSCALL_SOURCES}
        fs_io.cpp
        fs.cpp
        device_registry.cpp
        path_service.cpp
        filesystems/initrd/initrd.cpp
        initrd_init.cpp
        filesystems/ext4/ext4_file.cpp
        filesystems/ext4/ext4_mount.cpp
        filesystems/ext4/ext4_reader.cpp
        filesystems/tmpfs/tmpfs_file.cpp
        filesystems/tmpfs/tmpfs_mount.cpp
        filesystems/tmpfs/tmpfs_metadata.cpp
        filesystems/tmpfs/tmpfs_shared_memory.cpp
        unix_pathname.cpp
        init.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../ipc/pipe.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../ipc/unix_socket.cpp
)

target_include_directories(fs PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ASTIA_KERNEL_SOURCE_DIR}/include
        ${ASTIA_KERNEL_SOURCE_DIR}/src/arch
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc
        ${ASTIA_KERNEL_SOURCE_DIR}/src/sys
        ${ASTIA_KERNEL_SOURCE_DIR}/src/initrd
)

target_link_libraries(fs PUBLIC libcore mm initrd ipc)
