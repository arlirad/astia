add_library(proc STATIC
        proc.cpp
        process.cpp
        exec.cpp
        fork.cpp
        mmap.cpp
        syscalls.cpp
        thread.cpp
        fd_table.cpp
        scheduler.cpp
        init_proc.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../ipc/futex.cpp
)

target_include_directories(proc PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/sys
        ${CMAKE_SOURCE_DIR}/src/fs
)

target_link_libraries(proc PUBLIC libcore core mm arch_${ARCH} fs elf ipc)
