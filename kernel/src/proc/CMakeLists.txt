file(GLOB_RECURSE ASTIA_PROC_SYSCALL_SOURCES CONFIGURE_DEPENDS syscalls/*.cpp)
add_library(proc STATIC
        ${ASTIA_PROC_SYSCALL_SOURCES}
        cwd_context.cpp
        task_run.cpp
        proc.cpp
        process.cpp
        exec.cpp
        exec_hooks.cpp
        fork.cpp
        mmap.cpp
        signal_delivery.cpp
        thread.cpp
        fd_table.cpp
        scheduler.cpp
        init.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../ipc/futex.cpp
)

target_include_directories(proc PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ASTIA_KERNEL_SOURCE_DIR}/include
        ${ASTIA_KERNEL_SOURCE_DIR}/src/sys
        ${ASTIA_KERNEL_SOURCE_DIR}/src/fs
)

target_link_libraries(proc PUBLIC libcore core mm arch_${ARCH} fs elf ipc)
