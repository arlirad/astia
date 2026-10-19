add_library(fs STATIC
        fs.cpp
        devfs.cpp
        initrd_fs.cpp
        pipe.cpp
        initrd_init.cpp
        tmpfs.cpp
        syscalls.cpp
        init_fs.cpp
)

target_include_directories(fs PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/arch
        ${CMAKE_SOURCE_DIR}/src/proc
        ${CMAKE_SOURCE_DIR}/src/sys
        ${CMAKE_SOURCE_DIR}/src/initrd
)

target_link_libraries(fs PUBLIC libcore mm initrd)
