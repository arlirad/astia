add_library(proc STATIC
        elf_loader.cpp
        proc.cpp
        process.cpp
        exec.cpp
        fork.cpp
        mmap.cpp
        syscalls.cpp
        syscalls/kill.cpp
        syscalls/getpid.cpp
        syscalls/gettid.cpp
        syscalls/getpgrp.cpp
        syscalls/getpgid.cpp
        syscalls/setpgid.cpp
        syscalls/setsid.cpp
        syscalls/signal/sigaction.cpp
        syscalls/signal/sigaltstack.cpp
        syscalls/signal/sigreturn.cpp
        syscalls/signal/sigprocmask.cpp
        syscalls/signal/sigsuspend.cpp
        syscalls/signal/sigwait.cpp
        syscalls/process/exit.cpp
        syscalls/process/waitpid.cpp
        syscalls/process/chdir.cpp
        syscalls/process/getcwd.cpp
        syscalls/process/credentials.cpp
        syscalls/process/argv.cpp
        syscalls/process/spawnve.cpp
        syscalls/process/execve.cpp
        syscalls/process/fork.cpp
        syscalls/process/rlimit.cpp
        syscalls/memory/mmap.cpp
        syscalls/memory/munmap.cpp
        syscalls/ipc/futex.cpp
        syscalls/thread/yield.cpp
        syscalls/thread/thread_create.cpp
        syscalls/thread/thread_create2.cpp
        syscalls/thread/thread_exit.cpp
        syscalls/thread/thread_join.cpp
        syscalls/thread/thread_detach.cpp
        thread.cpp
        fd_table.cpp
        scheduler.cpp
        init_proc.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../ipc/futex.cpp
)

target_include_directories(proc PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ASTIA_KERNEL_SOURCE_DIR}/include
        ${ASTIA_KERNEL_SOURCE_DIR}/src/sys
        ${ASTIA_KERNEL_SOURCE_DIR}/src/fs
)

target_link_libraries(proc PUBLIC libcore core mm arch_${ARCH} fs elf ipc)
