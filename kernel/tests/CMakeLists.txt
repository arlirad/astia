# Host-side unit tests. Unlike everything else in the tree these build with the
# *system* toolchain and link against the host C library, so undo the
# freestanding compile/link settings inherited from the parent directory.
set(CMAKE_CXX_FLAGS "-Wall -Wextra")
set(CMAKE_C_FLAGS "-Wall -Wextra")
set_property(DIRECTORY PROPERTY LINK_OPTIONS "")
option(ASTIA_CROSS_USERSPACE "Use the Clang Astia sysroot for init_libc" OFF)
include(${ASTIA_KERNEL_SOURCE_DIR}/../cmake/AstiaCTest.cmake)

add_test(NAME astia_man_generator_layout
        COMMAND ${Python3_EXECUTABLE}
                ${ASTIA_KERNEL_SOURCE_DIR}/../tools/man/test_generate.py)
set_tests_properties(astia_man_generator_layout PROPERTIES
        LABELS "docs;deterministic")

find_package(Threads REQUIRED)
set(ASTIA_SYSCALL_HANDLER_SOURCES
        ${ASTIA_KERNEL_SOURCE_DIR}/src/fs/syscalls/io_destroy.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/fs/syscalls/io_register.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/fs/syscalls/io_setup.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/fs/syscalls/io_submit.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/fs/syscalls/mount.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/fs/syscalls/unmount.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/net/syscalls/accept.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/net/syscalls/bind.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/net/syscalls/connect.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/net/syscalls/getsockopt.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/net/syscalls/listen.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/net/syscalls/recv.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/net/syscalls/recvfrom.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/net/syscalls/send.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/net/syscalls/sendto.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/net/syscalls/shutdown.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/net/syscalls/socket.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/net/syscalls/socketpair.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/alarm.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/getpgid.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/getpgrp.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/getpid.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/getppid.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/gettid.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/ipc/futex.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/ipc/shm_open.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/ipc/shm_unlink.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/kill.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/lseek.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/memory/mmap.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/memory/mprotect.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/memory/msync.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/memory/munmap.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/process/chdir.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/process/execve.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/process/exit.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/process/fchdir.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/process/fork.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/process/getcwd.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/process/getegid.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/process/geteuid.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/process/getgid.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/process/getgroups.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/process/getrlimit.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/process/getsid.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/process/getuid.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/process/process_snapshot.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/process/setegid.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/process/seteuid.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/process/setgid.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/process/setgroups.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/process/setregid.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/process/setreuid.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/process/setrlimit.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/process/setuid.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/process/spawnve.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/process/waitpid.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/setpgid.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/setsid.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/signal/sigaction.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/signal/sigaltstack.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/signal/sigpending.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/signal/sigprocmask.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/signal/sigreturn.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/signal/sigsuspend.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/signal/sigtimedwait.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/signal/sigwait.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/thread/thread_cancel.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/thread/thread_cancel_pending.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/thread/thread_create.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/thread/thread_create2.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/thread/thread_create_owned.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/thread/thread_detach.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/thread/thread_exists.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/thread/thread_exit.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/thread/thread_getattr.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/thread/thread_join.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/thread/thread_kill.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/thread/yield.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/umask.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/sys/syscalls/clock_getres.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/sys/syscalls/clock_gettime.cpp
)

find_program(ASTIA_OBJDUMP NAMES llvm-objdump objdump REQUIRED)

# Fail at configure time with an actionable diagnostic instead of registering
# ELF tests that cannot be built later.
find_program(ASTIA_XXD xxd REQUIRED)
find_program(ASTIA_CPIO cpio REQUIRED)
find_program(ASTIA_GZIP gzip REQUIRED)
find_program(ASTIA_ZSTD zstd REQUIRED)

add_library(astia_test_support STATIC
        support/main.cpp
        support/arena.cpp
)
set_source_files_properties(support/main.cpp
        PROPERTIES COMPILE_OPTIONS "-fno-threadsafe-statics")
astia_apply_host_sanitizers(astia_test_support)
target_include_directories(astia_test_support PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/support
        ${ASTIA_KERNEL_SOURCE_DIR}/include
)

# The real kernel sources are compiled straight into the test binaries, so the
# tests exercise shipping code rather than copies.
set(_astia_test_includes
        ${ASTIA_KERNEL_SOURCE_DIR}/include
        ${ASTIA_KERNEL_SOURCE_DIR}/src/lib
        ${ASTIA_KERNEL_SOURCE_DIR}/src/ipc
        ${ASTIA_KERNEL_SOURCE_DIR}/src/mm
        ${ASTIA_KERNEL_SOURCE_DIR}/src/elf
        ${ASTIA_KERNEL_SOURCE_DIR}/src/initrd
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/thread
        ${ASTIA_KERNEL_SOURCE_DIR}/src/sys
        ${ASTIA_KERNEL_SOURCE_DIR}/src/io/aio
        ${ASTIA_KERNEL_SOURCE_DIR}/src/mod
        ${ASTIA_KERNEL_SOURCE_DIR}/src/fs
        ${ASTIA_KERNEL_SOURCE_DIR}/src/boot/virt
        ${ASTIA_KERNEL_SOURCE_DIR}/src/arch
        ${ASTIA_KERNEL_SOURCE_DIR}/src/arch/x86_64
)

add_library(astia_host_file_runtime OBJECT
        ${ASTIA_KERNEL_SOURCE_DIR}/src/fs/fs.cpp)
target_include_directories(astia_host_file_runtime PRIVATE ${_astia_test_includes})
target_compile_options(astia_host_file_runtime PRIVATE
        -fno-pie -fno-rtti -ffunction-sections -fdata-sections -fno-sanitize=all)

function(astia_add_host_test name)
    add_executable(${name} ${ARGN} support/freestanding_new.cpp)
    set_target_properties(${name} PROPERTIES LINK_OPTIONS "")
    target_link_libraries(${name} PRIVATE c astia_test_support)
    target_include_directories(${name} PRIVATE ${_astia_test_includes})
    target_compile_options(${name} PRIVATE -fno-threadsafe-statics)
    astia_apply_host_sanitizers(${name})
    add_test(NAME ${name} COMMAND ${name})
    astia_add_test_label(${name} "sanitizer-host")
endfunction()

# --- Pure logic: no hardware, no fake memory needed. ------------------------
astia_add_host_test(astia_memory_map_test memory_map_test.cpp)
astia_add_host_test(astia_pvh_test pvh_test.cpp ${ASTIA_KERNEL_SOURCE_DIR}/src/boot/virt/pvh.cpp)
astia_add_host_test(astia_multiboot2_test multiboot2_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/boot/virt/multiboot2.cpp)
astia_add_host_test(astia_boot_init_test boot_init_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/boot/init.cpp)
add_test(NAME astia_init_launch_shape
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/init_launch_shape.py
                ${ASTIA_KERNEL_SOURCE_DIR})
astia_add_test_label(astia_init_launch_shape "init;deterministic")

# error_t / optional<T>: header-only carriers for fallible kernel operations.
astia_add_host_test(astia_optional_test optional_test.cpp)
astia_add_host_test(astia_completion_test completion_test.cpp)
astia_add_host_test(astia_path_service_test
        path_service_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/fs/path_service.cpp)
astia_add_host_test(astia_task_test
        task_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/core/resume_queue.cpp
        support/resume_queue_host.cpp
)
astia_add_host_test(astia_fs_async_test
        fs_async_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/fs/fs_io.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/core/resume_queue.cpp
        support/resume_queue_host.cpp
)
target_compile_options(astia_fs_async_test PRIVATE -fno-sanitize=vptr)
target_link_options(astia_fs_async_test PRIVATE -fno-sanitize=vptr)
astia_add_host_test(astia_mapping_test mapping_test.cpp)
astia_add_host_test(astia_user_layout_test user_layout_test.cpp)
astia_add_host_test(astia_common_test common_test.cpp)
add_test(NAME astia_kernel_stack_usage_budget
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/stack_usage_budget.py
                ${ASTIA_OBJDUMP} $<TARGET_FILE:astiakrn_runtime> 0x3000)
set_tests_properties(astia_kernel_stack_usage_budget PROPERTIES
        LABELS "build;kernel;deterministic")
add_test(NAME astia_kernel_stack_usage_budget_fixtures
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/stack_usage_budget.py --fixture)
set_tests_properties(astia_kernel_stack_usage_budget_fixtures PROPERTIES
        LABELS "build;kernel;deterministic")
astia_add_host_test(astia_platform_test platform_test.cpp)
astia_add_host_test(astia_resource_limits_test resource_limits_test.cpp)
astia_add_host_test(astia_regs_test regs_test.cpp)
astia_add_host_test(astia_capabilities_test capabilities_test.cpp)
astia_add_host_test(astia_capabilities_header_test capabilities_header_test.cpp)
astia_add_host_test(astia_io_header_test io_header_test.cpp)
astia_add_host_test(astia_io_request_test io_request_test.cpp)
astia_add_host_test(astia_irq_header_test irq_header_test.cpp)
astia_add_host_test(astia_endian_test endian_test.cpp)
astia_add_host_test(astia_time_test
        time_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/sys/time.cpp)
target_compile_options(astia_time_test PRIVATE -ffunction-sections -fdata-sections)
target_link_options(astia_time_test PRIVATE -Wl,--gc-sections)
astia_add_host_test(astia_syscall_dispatch_test
        syscall_dispatch_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/sys/syscall.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/sys/handler_registry.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/core/resume_queue.cpp
        support/resume_queue_host.cpp
        ${ASTIA_SYSCALL_HANDLER_SOURCES})
target_compile_options(astia_syscall_dispatch_test PRIVATE -ffunction-sections -fdata-sections)
target_link_options(astia_syscall_dispatch_test PRIVATE -Wl,--gc-sections)
astia_add_host_test(astia_syscall_handler_prototype_test
        syscall_handler_prototype_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/sys/syscall.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/sys/handler_registry.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/core/resume_queue.cpp
        support/resume_queue_host.cpp
        ${ASTIA_SYSCALL_HANDLER_SOURCES})
target_compile_options(astia_syscall_handler_prototype_test PRIVATE -ffunction-sections -fdata-sections)
target_link_options(astia_syscall_handler_prototype_test PRIVATE -Wl,--gc-sections)
astia_add_host_test(astia_net_syscall_prototype_test
        net_syscall_prototype_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/sys/syscall.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/sys/handler_registry.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/core/resume_queue.cpp
        support/resume_queue_host.cpp
        ${ASTIA_SYSCALL_HANDLER_SOURCES})
target_compile_options(astia_net_syscall_prototype_test PRIVATE -ffunction-sections -fdata-sections)
target_link_options(astia_net_syscall_prototype_test PRIVATE -Wl,--gc-sections)
astia_add_host_test(astia_console_test
        console_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/console.cpp)
astia_add_host_test(astia_ownership_test ownership_test.cpp)
astia_add_host_test(astia_signal_test signal_test.cpp)
target_compile_definitions(astia_signal_test PRIVATE ASTIA_SIGNAL_HOST_TEST=1)
astia_add_host_test(astia_aio_test aio_test.cpp)
astia_add_host_test(astia_aio_filesystem_test aio_filesystem_test.cpp)
astia_add_host_test(astia_aio_readiness_test aio_readiness_test.cpp)
astia_add_host_test(astia_aio_socket_test aio_socket_test.cpp)
astia_add_host_test(astia_socket_abi_test socket_abi_test.cpp)
astia_add_host_test(astia_process_group_test process_group_test.cpp)
astia_add_host_test(astia_cwd_context_test
        cwd_context_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/cwd_context.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/fs/path_service.cpp
        support/resume_queue_host.cpp)
target_compile_options(astia_cwd_context_test PRIVATE -ffunction-sections -fdata-sections)
target_link_options(astia_cwd_context_test PRIVATE -Wl,--gc-sections)
astia_add_host_test(astia_tty_test
        tty_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/chr/tty.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/device.cpp
)
target_compile_definitions(astia_tty_test PRIVATE ASTIA_TTY_HOST_TEST=1)
target_include_directories(astia_tty_test PRIVATE ${ASTIA_KERNEL_SOURCE_DIR}/src/dev)
astia_add_host_test(astia_pty_test
        pty_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/chr/tty/pty.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/chr/tty.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/device.cpp
        )
target_include_directories(astia_pty_test PRIVATE ${ASTIA_KERNEL_SOURCE_DIR}/src/dev)
astia_add_host_test(astia_virtual_tty_test
        virtual_tty_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/console.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/chr/tty.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/chr/tty/virtual_tty.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/device.cpp)
target_include_directories(astia_virtual_tty_test PRIVATE ${ASTIA_KERNEL_SOURCE_DIR}/src/dev)
astia_add_host_test(astia_framebuffer_test
        framebuffer_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/chr/tty/psf_font.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/chr/tty/framebuffer_renderer.cpp)
target_include_directories(astia_framebuffer_test PRIVATE
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/chr/tty)
astia_add_host_test(astia_pseudo_test
        pseudo_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/chr/pseudo.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/device.cpp
)
target_include_directories(astia_pseudo_test PRIVATE ${ASTIA_KERNEL_SOURCE_DIR}/src/dev)

astia_add_host_test(astia_blkdev_test
        blkdev_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/blk/blkdev.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/device.cpp)
target_include_directories(astia_blkdev_test PRIVATE
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev)

astia_add_host_test(astia_device_class_names_test
        device_class_names_test.cpp)

astia_add_host_test(astia_device_test
        device_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/device.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/blk/blkdev.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/device_file.cpp
        support/device_ready_host.cpp)
target_sources(astia_device_test PRIVATE
        $<TARGET_OBJECTS:astia_host_file_runtime>)
target_compile_options(astia_device_test PRIVATE
        -fno-pie -fno-rtti -ffunction-sections -fdata-sections)
target_link_options(astia_device_test PRIVATE
        "-Wl,-no-pie" "-Wl,--gc-sections")
target_link_libraries(astia_device_test PRIVATE Threads::Threads)
astia_remove_test_label(astia_device_test "sanitizer-host")
astia_add_test_label(astia_device_test "sanitizer-host-partial")

astia_add_host_test(astia_pci_core_test
        pci_core_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/hw_pci.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/driver.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/device.cpp)
target_include_directories(astia_pci_core_test PRIVATE ${ASTIA_KERNEL_SOURCE_DIR}/src/dev)

astia_add_host_test(astia_pci_test
        pci_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/hw_pci.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/driver.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/device.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/mmio.cpp)
target_compile_definitions(astia_pci_test PRIVATE ASTIA_HOST_TEST=1)
target_include_directories(astia_pci_test PRIVATE ${ASTIA_KERNEL_SOURCE_DIR}/src/dev)

astia_add_host_test(astia_driver_manager_test
        driver_manager_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/driver.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/device.cpp)
target_include_directories(astia_driver_manager_test PRIVATE ${ASTIA_KERNEL_SOURCE_DIR}/src/dev)

astia_add_host_test(astia_net_test
        net_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/net/net.cpp)
set_tests_properties(astia_net_test PROPERTIES TIMEOUT 30)

astia_add_host_test(astia_mpmc_queue_test mpmc_queue_test.cpp)
target_link_libraries(astia_mpmc_queue_test PRIVATE Threads::Threads)
astia_add_host_test(astia_rcu_domain_test rcu_domain_test.cpp)

add_test(NAME astia_target_configuration
        COMMAND ${CMAKE_COMMAND}
        -DASTIA_KERNEL_SOURCE_DIR=${ASTIA_KERNEL_SOURCE_DIR}
        -DCMAKE_CURRENT_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/target_config_test.cmake)

add_test(NAME astia_selftest_arch_boundary
        COMMAND ${CMAKE_COMMAND}
        -DASTIA_KERNEL_SOURCE_DIR=${ASTIA_KERNEL_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/selftest_arch_boundary_test.cmake)

add_test(NAME astia_platform_api_regression
        COMMAND ${CMAKE_COMMAND}
        -DASTIA_KERNEL_SOURCE_DIR=${ASTIA_KERNEL_SOURCE_DIR}
        -DASTIA_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DASTIA_PLATFORM_API_TEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/platform_api_regression_test.cmake)
add_test(NAME astia_public_header_alone
        COMMAND ${CMAKE_COMMAND}
        -DASTIA_KERNEL_SOURCE_DIR=${ASTIA_KERNEL_SOURCE_DIR}
        -DASTIA_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DASTIA_PUBLIC_HEADER_TEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/public-header-alone
        -DASTIA_PUBLIC_HEADER_TEST_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/public_header_alone.cpp
        -P ${CMAKE_CURRENT_SOURCE_DIR}/public_header_alone_test.cmake)
set_tests_properties(astia_public_header_alone
        PROPERTIES LABELS "kernel;headers;deterministic")

add_test(NAME astia_architecture_boundaries
        COMMAND ${CMAKE_COMMAND}
        -DASTIA_KERNEL_SOURCE_DIR=${ASTIA_KERNEL_SOURCE_DIR}
        -DASTIA_ARCHITECTURE_BOUNDARIES_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/architecture_boundaries.py
        -DASTIA_PYTHON=${Python3_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/architecture_boundaries_test.cmake)
set_tests_properties(astia_architecture_boundaries
        PROPERTIES LABELS "kernel;architecture;deterministic")

add_test(NAME astia_kernel_warning_flags
        COMMAND ${CMAKE_COMMAND}
        -DASTIA_MODE=kernel
        -DASTIA_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/warning-regression/kernel
        -DASTIA_COMPILE_COMMANDS=${CMAKE_BINARY_DIR}/compile_commands.json
        -P ${CMAKE_CURRENT_SOURCE_DIR}/production_warning_flags_test.cmake)
add_test(NAME astia_nvme_warning_flags
        COMMAND ${CMAKE_COMMAND}
        -DASTIA_MODE=module_cxx
        -DASTIA_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/warning-regression/nvme.o
        -DASTIA_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DASTIA_KERNEL_INCLUDE=${ASTIA_KERNEL_SOURCE_DIR}/include
        -DASTIA_SOURCE=${ASTIA_KERNEL_SOURCE_DIR}/../modules/nvme/nvme.cpp
        -P ${CMAKE_CURRENT_SOURCE_DIR}/production_warning_flags_test.cmake)
add_test(NAME astia_virtio_blk_warning_flags
        COMMAND ${CMAKE_COMMAND}
        -DASTIA_MODE=module_cxx
        -DASTIA_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/warning-regression/virtio_blk.o
        -DASTIA_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DASTIA_KERNEL_INCLUDE=${ASTIA_KERNEL_SOURCE_DIR}/include
        -DASTIA_SOURCE=${ASTIA_KERNEL_SOURCE_DIR}/../modules/virtio-blk/virtio_blk.cpp
        -P ${CMAKE_CURRENT_SOURCE_DIR}/production_warning_flags_test.cmake)

# --- ELF fixtures: compile real images with the host toolchain and byte-embed
# them, so the loader sees genuine gcc output rather than hand-written ELF.
set(_fix_dir ${CMAKE_CURRENT_BINARY_DIR}/fixtures)
set(ASTIA_MODULES_SOURCE_DIR ${ASTIA_KERNEL_SOURCE_DIR}/../modules)

# Embed an object as a C++ TU exposing `unsigned char <blob_name>[]` + `_len`.
function(astia_embed_blob out_cpp obj_path blob_name)
    set(_cpp ${obj_path}.cpp)
    add_custom_command(
            OUTPUT ${_cpp}
            COMMAND ${CMAKE_COMMAND} -E env
                    XXD=${ASTIA_XXD}
                    sh ${CMAKE_CURRENT_SOURCE_DIR}/embed_blob.sh
                    ${obj_path} ${blob_name} ${_cpp}
            DEPENDS ${obj_path} ${CMAKE_CURRENT_SOURCE_DIR}/embed_blob.sh
            COMMENT "Embedding ${blob_name}"
            VERBATIM
    )
    set(${out_cpp} ${_cpp} PARENT_SCOPE)
endfunction()

# ET_REL module object (relocatable): imports kernel_add, has module_main.
add_custom_command(
        OUTPUT ${_fix_dir}/hello_mod.o
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -c -ffreestanding -fno-pic -fno-pie
        -mcmodel=large -fno-exceptions -fno-stack-protector
        -fno-asynchronous-unwind-tables -fno-unwind-tables
        -o ${_fix_dir}/hello_mod.o ${ASTIA_MODULES_SOURCE_DIR}/tests/hello_mod.cpp
        DEPENDS ${ASTIA_MODULES_SOURCE_DIR}/tests/hello_mod.cpp
        COMMENT "Compiling ET_REL fixture hello_mod.o"
)
astia_embed_blob(_hello_mod_cpp ${_fix_dir}/hello_mod.o hello_mod_blob)

add_custom_command(
        OUTPUT ${_fix_dir}/module_symbols.o
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -c -ffreestanding -fno-pic -fno-pie
        -fno-exceptions -fno-rtti -fno-use-cxa-atexit -fno-stack-protector
        -fno-asynchronous-unwind-tables -fno-unwind-tables
        -mcmodel=large -o ${_fix_dir}/module_symbols.o
        ${CMAKE_CURRENT_SOURCE_DIR}/module_symbols.cpp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/module_symbols.cpp
        COMMENT "Compiling C++ ET_REL symbol fixture"
)
astia_embed_blob(_module_symbols_cpp ${_fix_dir}/module_symbols.o module_symbols_blob)

add_custom_command(
        OUTPUT ${_fix_dir}/module_priority_malformed.o
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -c -ffreestanding -fno-pic -fno-pie
        -mcmodel=large -fno-exceptions -fno-rtti -fno-use-cxa-atexit
        -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables
        -o ${_fix_dir}/module_priority_malformed.o
        ${CMAKE_CURRENT_SOURCE_DIR}/module_priority_malformed_fixture.cpp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/module_priority_malformed_fixture.cpp
        COMMENT "Compiling ET_REL malformed priority fixture"
)
astia_embed_blob(_module_priority_malformed_cpp
        ${_fix_dir}/module_priority_malformed.o module_priority_malformed_blob)

add_custom_command(
        OUTPUT ${_fix_dir}/module_priority_overflow.o
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -c -ffreestanding -fno-pic -fno-pie
        -mcmodel=large -fno-exceptions -fno-rtti -fno-use-cxa-atexit
        -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables
        -o ${_fix_dir}/module_priority_overflow.o
        ${CMAKE_CURRENT_SOURCE_DIR}/module_priority_overflow_fixture.cpp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/module_priority_overflow_fixture.cpp
        COMMENT "Compiling ET_REL overflow priority fixture"
)
astia_embed_blob(_module_priority_overflow_cpp
        ${_fix_dir}/module_priority_overflow.o module_priority_overflow_blob)

foreach(_astia_priority_trailing_dot_fixture init fini)
    add_custom_command(
            OUTPUT ${_fix_dir}/module_priority_${_astia_priority_trailing_dot_fixture}_trailing_dot.o
            COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}
            COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -c -ffreestanding -fno-pic -fno-pie
            -mcmodel=large -fno-exceptions -fno-rtti -fno-use-cxa-atexit
            -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables
            -o ${_fix_dir}/module_priority_${_astia_priority_trailing_dot_fixture}_trailing_dot.o
            ${CMAKE_CURRENT_SOURCE_DIR}/module_priority_${_astia_priority_trailing_dot_fixture}_trailing_dot_fixture.cpp
            DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/module_priority_${_astia_priority_trailing_dot_fixture}_trailing_dot_fixture.cpp
            COMMENT "Compiling ET_REL trailing-dot ${_astia_priority_trailing_dot_fixture} priority fixture"
    )
    astia_embed_blob(
            _module_priority_${_astia_priority_trailing_dot_fixture}_trailing_dot_cpp
            ${_fix_dir}/module_priority_${_astia_priority_trailing_dot_fixture}_trailing_dot.o
            module_priority_${_astia_priority_trailing_dot_fixture}_trailing_dot_blob)
endforeach()

add_custom_command(
        OUTPUT ${_fix_dir}/module_lifecycle.o
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -c -ffreestanding -fno-pic -fno-pie
        -mcmodel=large -fno-exceptions -fno-rtti -fno-use-cxa-atexit
        -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables
        -o ${_fix_dir}/module_lifecycle.o
        ${CMAKE_CURRENT_SOURCE_DIR}/module_lifecycle_fixture.cpp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/module_lifecycle_fixture.cpp
        COMMENT "Compiling ET_REL lifecycle fixture"
)
astia_embed_blob(_lifecycle_fixture_cpp
         ${_fix_dir}/module_lifecycle.o module_lifecycle_blob)

add_custom_command(
        OUTPUT ${_fix_dir}/module_lifecycle_atexit.o
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -c -ffreestanding -fno-pic -fno-pie
        -mcmodel=large -fno-exceptions -fno-rtti -fno-use-cxa-atexit
        -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables
        -o ${_fix_dir}/module_lifecycle_atexit.o
        ${CMAKE_CURRENT_SOURCE_DIR}/module_lifecycle_atexit_fixture.cpp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/module_lifecycle_atexit_fixture.cpp
        COMMENT "Compiling ET_REL atexit lifecycle fixture"
)
astia_embed_blob(_lifecycle_atexit_fixture_cpp
        ${_fix_dir}/module_lifecycle_atexit.o module_lifecycle_atexit_blob)

foreach(_lifecycle_extra_fixture limit dso)
    add_custom_command(
            OUTPUT ${_fix_dir}/module_lifecycle_${_lifecycle_extra_fixture}.o
            COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}
            COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -c -ffreestanding -fno-pic -fno-pie
            -mcmodel=large -fno-exceptions -fno-rtti -fno-use-cxa-atexit
            -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables
            -o ${_fix_dir}/module_lifecycle_${_lifecycle_extra_fixture}.o
            ${CMAKE_CURRENT_SOURCE_DIR}/module_lifecycle_${_lifecycle_extra_fixture}_fixture.cpp
            DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/module_lifecycle_${_lifecycle_extra_fixture}_fixture.cpp
            COMMENT "Compiling ET_REL ${_lifecycle_extra_fixture} lifecycle fixture"
    )
    astia_embed_blob(_lifecycle_${_lifecycle_extra_fixture}_fixture_cpp
            ${_fix_dir}/module_lifecycle_${_lifecycle_extra_fixture}.o
            module_lifecycle_${_lifecycle_extra_fixture}_blob)
endforeach()

add_custom_command(
        OUTPUT ${_fix_dir}/module_lifecycle_no_fini.o
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -c -ffreestanding -fno-pic -fno-pie
        -mcmodel=large -fno-exceptions -fno-rtti -fno-use-cxa-atexit
        -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables
        -o ${_fix_dir}/module_lifecycle_no_fini.o
        ${CMAKE_CURRENT_SOURCE_DIR}/module_lifecycle_no_fini_fixture.cpp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/module_lifecycle_no_fini_fixture.cpp
        COMMENT "Compiling ET_REL no-fini lifecycle fixture"
)
astia_embed_blob(_lifecycle_no_fini_fixture_cpp
        ${_fix_dir}/module_lifecycle_no_fini.o module_lifecycle_no_fini_blob)

add_custom_command(
        OUTPUT ${_fix_dir}/module_lifecycle_priority.o
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -c -ffreestanding -fno-pic -fno-pie
        -mcmodel=large -fno-exceptions -fno-rtti -fno-use-cxa-atexit
        -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables
        -o ${_fix_dir}/module_lifecycle_priority.o
        ${CMAKE_CURRENT_SOURCE_DIR}/module_lifecycle_priority_fixture.cpp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/module_lifecycle_priority_fixture.cpp
        COMMENT "Compiling ET_REL lifecycle priority fixture"
)
astia_embed_blob(_lifecycle_priority_fixture_cpp
        ${_fix_dir}/module_lifecycle_priority.o module_lifecycle_priority_blob)

# ET_DYN (PIE) executable: self-contained, entry at fixture_entry, with a data
# pointer that forces an R_X86_64_RELATIVE dynamic relocation.
add_custom_command(
        OUTPUT ${_fix_dir}/fixture_dyn.so
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}
        COMMAND ${CMAKE_C_COMPILER} -shared -fPIC -nostdlib -O0
        -Wl,-e,fixture_entry
        -o ${_fix_dir}/fixture_dyn.so ${CMAKE_CURRENT_SOURCE_DIR}/fixture_exec.c
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/fixture_exec.c
        COMMENT "Compiling ET_DYN fixture fixture_dyn.so"
)
astia_embed_blob(_fixture_dyn_cpp ${_fix_dir}/fixture_dyn.so fixture_dyn_blob)

# ET_EXEC (fixed-address) executable, linked at 0x10000000.
add_custom_command(
        OUTPUT ${_fix_dir}/fixture_exec.elf
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}
        COMMAND ${CMAKE_C_COMPILER} -nostdlib -O0 -Wl,-no-pie
        -Wl,-Ttext-segment=0x10000000 -Wl,-e,fixture_entry
        -o ${_fix_dir}/fixture_exec.elf ${CMAKE_CURRENT_SOURCE_DIR}/fixture_exec.c
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/fixture_exec.c
        COMMENT "Compiling ET_EXEC fixture fixture_exec.elf"
)
astia_embed_blob(_fixture_exec_cpp ${_fix_dir}/fixture_exec.elf fixture_exec_blob)

# elf: the generic loader, exercised end-to-end against all three fixtures.
astia_add_host_test(astia_elf_test
        elf_test.cpp
        ${_hello_mod_cpp}
        ${_module_symbols_cpp}
        ${_module_priority_malformed_cpp}
        ${_module_priority_overflow_cpp}
        ${_module_priority_init_trailing_dot_cpp}
        ${_module_priority_fini_trailing_dot_cpp}
        ${_fixture_dyn_cpp}
        ${_fixture_exec_cpp}
        ${ASTIA_KERNEL_SOURCE_DIR}/src/elf/elf.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/elf/exec_loader.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/elf/reloc_loader.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/core/resume_queue.cpp
        support/resume_queue_host.cpp
)

add_custom_command(
        OUTPUT ${_fix_dir}/initrd_newc.cpio
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}
        COMMAND ${CMAKE_COMMAND} -E env ASTIA_CPIO=${ASTIA_CPIO}
                sh ${CMAKE_CURRENT_SOURCE_DIR}/make_cpio_initrd.sh newc
                ${_fix_dir}/initrd_newc.cpio
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/make_cpio_initrd.sh
        COMMENT "Building newc initrd fixture"
)
astia_embed_blob(_initrd_newc_cpp ${_fix_dir}/initrd_newc.cpio initrd_newc_blob)

add_custom_command(
        OUTPUT ${_fix_dir}/initrd_crc.cpio
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}
        COMMAND ${CMAKE_COMMAND} -E env ASTIA_CPIO=${ASTIA_CPIO}
                sh ${CMAKE_CURRENT_SOURCE_DIR}/make_cpio_initrd.sh crc
                ${_fix_dir}/initrd_crc.cpio
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/make_cpio_initrd.sh
        COMMENT "Building crc initrd fixture"
)
astia_embed_blob(_initrd_crc_cpp ${_fix_dir}/initrd_crc.cpio initrd_crc_blob)

add_custom_command(
        OUTPUT ${_fix_dir}/initrd_newc.cpio.gz
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/compress_initrd.sh gzip
                ${ASTIA_GZIP} ${_fix_dir}/initrd_newc.cpio
                ${_fix_dir}/initrd_newc.cpio.gz
        DEPENDS ${_fix_dir}/initrd_newc.cpio
                ${CMAKE_CURRENT_SOURCE_DIR}/compress_initrd.sh
        COMMENT "Building gzip initrd fixture"
)
astia_embed_blob(_initrd_gzip_cpp ${_fix_dir}/initrd_newc.cpio.gz initrd_gzip_blob)

add_custom_command(
        OUTPUT ${_fix_dir}/initrd_newc.cpio.zst
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/compress_initrd.sh zstd
                ${ASTIA_ZSTD} ${_fix_dir}/initrd_newc.cpio
                ${_fix_dir}/initrd_newc.cpio.zst
        DEPENDS ${_fix_dir}/initrd_newc.cpio
                ${CMAKE_CURRENT_SOURCE_DIR}/compress_initrd.sh
        COMMENT "Building zstd initrd fixture"
)
astia_embed_blob(_initrd_zstd_cpp ${_fix_dir}/initrd_newc.cpio.zst initrd_zstd_blob)

astia_add_host_test(astia_initrd_test
        initrd_test.cpp
        ${_initrd_newc_cpp}
        ${_initrd_crc_cpp}
        ${_initrd_gzip_cpp}
        ${_initrd_zstd_cpp}
        ${ASTIA_KERNEL_SOURCE_DIR}/src/initrd/initrd.cpp
)

# mod: the linked kernel symbol resolver (the ELF loader itself is covered by astia_elf_test).
astia_add_host_test(astia_mod_test
        mod_test.cpp
        kernel_symbol_table_fixture.S
        ${ASTIA_KERNEL_SOURCE_DIR}/src/mod/exports.cpp
)

astia_add_host_test(astia_module_msix_test
        module_msix_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/mod/module_driver.cpp
)
target_compile_options(astia_module_msix_test PRIVATE
        -ffunction-sections -fdata-sections)
target_link_options(astia_module_msix_test PRIVATE
        "-Wl,-no-pie" "-Wl,--gc-sections")

astia_add_host_test(astia_module_lifecycle_test
         module_lifecycle_test.cpp
         ${_lifecycle_fixture_cpp}
         ${_lifecycle_atexit_fixture_cpp}
         ${_lifecycle_limit_fixture_cpp}
         ${_lifecycle_dso_fixture_cpp}
        ${_lifecycle_no_fini_fixture_cpp}
         ${_lifecycle_priority_fixture_cpp}
         ${ASTIA_KERNEL_SOURCE_DIR}/src/mod/module.cpp
         module_cxxabi_host_test.cpp
         ${ASTIA_KERNEL_SOURCE_DIR}/src/elf/elf.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/elf/exec_loader.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/elf/reloc_loader.cpp
)
target_compile_options(astia_module_lifecycle_test PRIVATE
        -ffunction-sections -fdata-sections)
target_compile_definitions(astia_module_lifecycle_test PRIVATE
        ASTIA_MODULE_HOST_TEST=1)
target_link_options(astia_module_lifecycle_test PRIVATE
        "-Wl,-no-pie" "-Wl,--gc-sections")
add_test(NAME astia_kernel_symbol_visibility
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/symbol_table_visibility_test.py
                ${CMAKE_CXX_COMPILER}
                ${ASTIA_KERNEL_SOURCE_DIR}/scripts/generate_module_symbols.py)

astia_add_host_test(astia_ext4_test
        ext4_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/fs/ext4/ext4_reader.cpp
)
find_program(ASTIA_MKFS_EXT4 mkfs.ext4)
find_program(ASTIA_E2FSCK e2fsck)
if (ASTIA_MKFS_EXT4 AND ASTIA_E2FSCK)
    set(_ext4_real_fixture ${CMAKE_CURRENT_BINARY_DIR}/fixtures/ext4-metadata-csum.img)
    add_custom_command(
            OUTPUT ${_ext4_real_fixture}
            COMMAND ${CMAKE_COMMAND} -E make_directory
                    ${CMAKE_CURRENT_BINARY_DIR}/fixtures
            COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/make_ext4_fixture.sh
                    ${_ext4_real_fixture}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/make_ext4_fixture.sh
            COMMENT "Building metadata-checksummed ext4 fixture"
            VERBATIM
    )
    add_custom_target(astia_ext4_real_fixture DEPENDS ${_ext4_real_fixture})
    add_executable(astia_ext4_real_test
            ext4_real_test.cpp
            ${ASTIA_KERNEL_SOURCE_DIR}/src/fs/ext4/ext4_reader.cpp
    )
    set_target_properties(astia_ext4_real_test PROPERTIES LINK_OPTIONS "")
    astia_apply_host_sanitizers(astia_ext4_real_test)
    target_include_directories(astia_ext4_real_test PRIVATE ${_astia_test_includes})
    add_dependencies(astia_ext4_real_test astia_ext4_real_fixture)
    add_test(NAME astia_ext4_real_test
            COMMAND astia_ext4_real_test ${_ext4_real_fixture})
    astia_add_test_label(astia_ext4_real_test "sanitizer-host")
endif ()

# memory.cpp is the code under test here, so it overrides the host libc's
# symbols; -fno-builtin stops the compiler rewriting our loops back into calls.
astia_add_host_test(astia_memory_test memory_test.cpp ${ASTIA_KERNEL_SOURCE_DIR}/src/lib/memory.cpp)
set_source_files_properties(${ASTIA_KERNEL_SOURCE_DIR}/src/lib/memory.cpp
        PROPERTIES COMPILE_OPTIONS "-fno-builtin")

# Substrate-bound logic: frame_allocator and page_map treat physical addresses
# as pointers. Back a fixed mmap'd window with real memory (support/arena.cpp)
# and inject symbols the allocator reserves. The addresses must match
# astia_test::arena_base / kernel_image_size in support/arena.hpp.
function(astia_add_arena_test name)
    astia_add_host_test(${name} ${ARGN})
    # The __kernel_* values are absolute; build non-PIE so the dynamic loader
    # does not add the load bias to them (which would move the bitmap far away).
    target_compile_options(${name} PRIVATE -fno-pie)
    target_link_options(${name} PRIVATE
            "-Wl,-no-pie"
            "-Wl,--defsym=__kernel_start=0x08000000"
            "-Wl,--defsym=__kernel_end=0x08002000"
    )
    # Compiles out the privileged invlpg / mov cr3 in page_map.cpp.
    target_compile_definitions(${name} PRIVATE ASTIA_HOST_TEST=1)
endfunction()

astia_add_arena_test(astia_exec_rollback_test
        exec_rollback_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/arch/x86_64/address_space.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/arch/x86_64/page_map.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/arch/x86_64/layout.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/mm/frame_allocator.cpp
)

astia_add_arena_test(astia_rcu_hashmap_test
        rcu_hashmap_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/mm/heap.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/mm/frame_allocator.cpp
        support/interrupts_host.cpp)
target_link_libraries(astia_rcu_hashmap_test PRIVATE Threads::Threads)

astia_add_arena_test(astia_frame_allocator_test
        frame_allocator_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/mm/frame_allocator.cpp
)
astia_add_arena_test(astia_page_map_test
        page_map_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/arch/x86_64/page_map.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/arch/x86_64/layout.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/mm/frame_allocator.cpp
)
astia_add_arena_test(astia_address_space_test
        address_space_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/arch/x86_64/address_space.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/arch/x86_64/page_map.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/arch/x86_64/layout.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/mm/frame_allocator.cpp
)

# mm::heap: the free-list allocator over a window backed by arena frames.
astia_add_arena_test(astia_heap_test
        heap_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/mm/heap.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/mm/frame_allocator.cpp
        support/interrupts_host.cpp
)

# mm::phys_region: region-local reference counting and the CoW
# parent/child links — no kernel-wide per-frame table anywhere.
astia_add_arena_test(astia_phys_region_test
        phys_region_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/mm/phys_region.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/mm/pinned_buffer.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/mm/iovec.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/mm/heap.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/mm/frame_allocator.cpp
        support/interrupts_host.cpp
)

astia_add_arena_test(astia_ext4_vfs_test
        ext4_vfs_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/device.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/blk/blkdev.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/device_file.cpp
        support/pty_vfs_host.cpp
        support/interrupts_host.cpp
)
target_link_libraries(astia_ext4_vfs_test PRIVATE fs)
target_compile_options(astia_ext4_vfs_test PRIVATE -fno-rtti)
target_include_directories(astia_ext4_vfs_test PRIVATE
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev)
astia_remove_test_label(astia_ext4_vfs_test "sanitizer-host")
astia_add_test_label(astia_ext4_vfs_test "sanitizer-host-partial")

# On-target integration test: boot the real kernel under QEMU and let its
# isa-debug-exit status decide pass/fail. Covers what host tests cannot (CR3,
# TLB, real physical memory, the boot path itself).
if (ARCH STREQUAL "x86_64")
    find_program(ASTIA_QEMU qemu-system-x86_64)
    find_program(GRUB_FILE grub-file)
    find_program(GRUB_MKRESCUE grub-mkrescue)
    find_program(XORRISO xorriso)
    add_library(astia_production_exit_init OBJECT
            ${CMAKE_CURRENT_SOURCE_DIR}/init_exit.c)
    target_include_directories(astia_production_exit_init PRIVATE
            ${ASTIA_KERNEL_SOURCE_DIR}/include)
    target_compile_options(astia_production_exit_init PRIVATE
            -ffreestanding -fno-pic -fno-pie -fno-builtin -fno-stack-protector
            -fno-asynchronous-unwind-tables -mno-red-zone)
    if (ASTIA_QEMU)
        add_test(NAME astia_kernel_boot
                COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_boot_test.sh
                ${ASTIA_QEMU} $<TARGET_FILE:astiakrn_test>
                ""
                "pci: enumerated"
                "selftest: interrupts ok (lapic)"
                "selftest: clock ok"
                "selftest: statx ok"
        )
        set_tests_properties(astia_kernel_boot PROPERTIES TIMEOUT 60)

        add_test(NAME astia_fs_registry_boot
                COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_boot_test.sh
                ${ASTIA_QEMU} $<TARGET_FILE:astiakrn_test>
                ""
                "filesystem registry ok"
        )
        set_tests_properties(astia_fs_registry_boot PROPERTIES TIMEOUT 60)

        # Repeat the process/thread/ring lifecycle with two virtual CPUs. The
        # kernel still has one scheduling domain; this deliberately changes
        # only QEMU's interrupt topology so teardown paths are exercised under
        # the multi-vCPU configuration without broad SMP policy changes.
        add_test(NAME astia_kernel_lifecycle_smp2
                COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_boot_test.sh
                ${ASTIA_QEMU} $<TARGET_FILE:astiakrn_test>
                ""
                "selftest: interrupts ok (lapic)"
                "selftest: clock ok"
                "selftest: statx ok"
                "smp: discovered=2 online=1"
                "smp: reschedule IPI armed"
        )
        set_tests_properties(astia_kernel_lifecycle_smp2 PROPERTIES
                TIMEOUT 60 ENVIRONMENT "ASTIA_QEMU_SMP=2")

        # Keep a focused SMP boot assertion in addition to the lifecycle run.
        add_test(NAME astia_kernel_smp_boot
                COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_boot_test.sh
                ${ASTIA_QEMU} $<TARGET_FILE:astiakrn_test>
                ""
                "smp: discovered=2 online=1"
                "smp: reschedule IPI armed"
        )
        set_tests_properties(astia_kernel_smp_boot PROPERTIES
                TIMEOUT 60 ENVIRONMENT "ASTIA_QEMU_SMP=2")

        add_test(NAME astia_kernel_scheduler_smp2
                COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_boot_test.sh
                ${ASTIA_QEMU} $<TARGET_FILE:astiakrn_test>
                ""
                "selftest: scheduler ok"
                "smp: discovered=2")
        set_tests_properties(astia_kernel_scheduler_smp2 PROPERTIES
                TIMEOUT 60 ENVIRONMENT "ASTIA_QEMU_SMP=2;ASTIA_QEMU_MACHINE=q35")

        # End-to-end initrd: the same kernel, but the boot module is a cpio image
        # carrying a self-contained ET_REL module. QEMU's PVH path puts -initrd in
        # the modlist, so this covers modlist -> cpio walk -> ELF load -> run.
        add_custom_command(
                OUTPUT ${_fix_dir}/hello_initrd.o
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}
                COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -c -ffreestanding -fno-pic -fno-pie
                -mcmodel=large -fno-stack-protector -fno-asynchronous-unwind-tables
                -o ${_fix_dir}/hello_initrd.o ${ASTIA_MODULES_SOURCE_DIR}/tests/hello_initrd.cpp
                DEPENDS ${ASTIA_MODULES_SOURCE_DIR}/tests/hello_initrd.cpp
                COMMENT "Compiling ET_REL fixture hello_initrd.o"
        )
        # The libc in ring 3: the same sources the host tests link, but
        # freestanding and non-PIE, with the AIO ports as Astia's side of the
        # seams.  Userspace images link this against tests/crt0.S.
        add_library(astia_userspace_libc STATIC
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/assert.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/aio.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/backend_aio.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/atfork.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/ctype.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/dirent.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/errno.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/file.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/group.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/pwd.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/ioctl.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/locale.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/malloc.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/math.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/mman.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/process.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/process_aio.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/path.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/poll.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/prctl.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/pthread.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/printf.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/rlimit.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/scanf.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/setjmp.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/signal.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/semaphore.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/socket.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/snprintf.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/select.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/stdlib.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/stat.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/string.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/termcap.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/termios.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/time.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/time_aio.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/time_sleep_aio.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/times.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/unistd.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/utsname.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/uio.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/wchar.c
        )
        target_include_directories(astia_userspace_libc PRIVATE
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/include
                ${ASTIA_KERNEL_SOURCE_DIR}/include
        )
        target_compile_options(astia_userspace_libc PRIVATE
                -ffreestanding -fno-pic -fno-pie -fno-stack-protector
                -fno-asynchronous-unwind-tables -mno-red-zone -fno-builtin
        )

        # A real userspace program: CRT0 unpacks the kernel's startup
        # frame into main(argc, argv, envp) and leaves via libc exit().
        # Prefer the standalone Clang sysroot so this image cannot silently
        # depend on the host compiler's headers or libgcc.
        find_program(ASTIA_CLANG clang)
        add_custom_command(
                OUTPUT ${_fix_dir}/crt0.o
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}
                COMMAND ${CMAKE_C_COMPILER} -c -ffreestanding -fno-pic -fno-pie -fno-builtin
                -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone
                -o ${_fix_dir}/crt0.o ${CMAKE_CURRENT_SOURCE_DIR}/crt0.S
                DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/crt0.S
                COMMENT "Compiling userspace CRT0 fixture")
        if (ASTIA_CROSS_USERSPACE AND ASTIA_CLANG)
            set(_cross_dir ${_fix_dir}/clang)
            file(GLOB _cross_libc_sources CONFIGURE_DEPENDS
                    ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src/*.c)
            add_custom_command(
                    OUTPUT ${_cross_dir}/image/init_libc
                    COMMAND ${CMAKE_COMMAND} -E env
                    ASTIA_CROSS_BUILD=${_cross_dir}
                    ASTIA_SYSROOT=${_cross_dir}/sysroot
                    CLANG=${ASTIA_CLANG}
                    ${ASTIA_KERNEL_SOURCE_DIR}/../toolchain/build-init-libc.sh
                    DEPENDS
                    ${ASTIA_KERNEL_SOURCE_DIR}/../toolchain/build-init-libc.sh
                    ${ASTIA_KERNEL_SOURCE_DIR}/../toolchain/astia-x86_64-clang.cmake
                    ${CMAKE_CURRENT_SOURCE_DIR}/crt0.S
                    ${CMAKE_CURRENT_SOURCE_DIR}/init_libc.c
                    ${_cross_libc_sources}
                    ${ASTIA_KERNEL_SOURCE_DIR}/../libc/CMakeLists.txt
                    COMMENT "Cross-building libc-backed userspace image init_libc"
            )
            set(_init_libc_image ${_cross_dir}/image/init_libc)
        else ()
            add_custom_command(
                    OUTPUT ${_fix_dir}/init_libc
                    COMMAND ${CMAKE_C_COMPILER} -c -ffreestanding -fno-pic -fno-pie
                    -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone
                    -fno-builtin -I${ASTIA_KERNEL_SOURCE_DIR}/include
                    -I${ASTIA_KERNEL_SOURCE_DIR}/../libc/include
                    -o ${_fix_dir}/init_libc_main.o
                    ${CMAKE_CURRENT_SOURCE_DIR}/init_libc.c
                    COMMAND ${CMAKE_C_COMPILER} -nostdlib -static -Wl,-no-pie
                    -Wl,-e,_start -Wl,-Ttext-segment=0x40000000 -lgcc
                    -o ${_fix_dir}/init_libc ${_fix_dir}/crt0.o
                    ${_fix_dir}/init_libc_main.o $<TARGET_FILE:astia_userspace_libc>
                    DEPENDS ${_fix_dir}/crt0.o
                    ${CMAKE_CURRENT_SOURCE_DIR}/init_libc.c astia_userspace_libc
                    COMMENT "Linking libc-backed userspace image init_libc"
            )
            set(_init_libc_image ${_fix_dir}/init_libc)
        endif ()

    add_custom_command(
            OUTPUT ${_fix_dir}/boot_initrd.cpio
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/boot_stage
                COMMAND ${CMAKE_COMMAND} -E copy
                ${_fix_dir}/hello_initrd.o ${_fix_dir}/boot_stage/
                COMMAND ${CMAKE_C_COMPILER} -c -ffreestanding -fno-pic -fno-pie -fno-builtin
                -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                -o ${_fix_dir}/init_user.o ${CMAKE_CURRENT_SOURCE_DIR}/init_user.c
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -static -Wl,-no-pie
                -Wl,-e,_start -Wl,-Ttext-segment=0x40000000
                -o ${_fix_dir}/init_user ${_fix_dir}/init_user.o
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/boot_stage/sbin
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/boot_stage/etc
                COMMAND ${CMAKE_COMMAND} -E copy ${ASTIA_KERNEL_SOURCE_DIR}/../daemond/passwd ${_fix_dir}/boot_stage/etc/passwd
                COMMAND ${CMAKE_COMMAND} -E copy ${ASTIA_KERNEL_SOURCE_DIR}/../daemond/group ${_fix_dir}/boot_stage/etc/group
                COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/make_truncated_elf.sh
                ${_fix_dir}/fixture_exec.elf ${_fix_dir}/boot_stage/sbin/truncated-elf
                COMMAND ${CMAKE_COMMAND} -E copy
                ${_fix_dir}/init_user ${_fix_dir}/boot_stage/sbin/init
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/boot_stage/bin
                COMMAND ${CMAKE_COMMAND} -E copy
                ${_init_libc_image} ${_fix_dir}/boot_stage/bin/init_libc
                COMMAND ${CMAKE_COMMAND} -E env ASTIA_CPIO=${ASTIA_CPIO}
                sh ${CMAKE_CURRENT_SOURCE_DIR}/make_cpio_archive.sh
                ${_fix_dir}/boot_stage ${_fix_dir}/boot_initrd.cpio
                hello_initrd.o sbin/init sbin/truncated-elf bin/init_libc etc/passwd etc/group
                DEPENDS ${_fix_dir}/hello_initrd.o ${_fix_dir}/fixture_exec.elf
                ${CMAKE_CURRENT_SOURCE_DIR}/make_truncated_elf.sh
                ${CMAKE_CURRENT_SOURCE_DIR}/init_user.c ${_init_libc_image}
                ${ASTIA_KERNEL_SOURCE_DIR}/../daemond/passwd ${ASTIA_KERNEL_SOURCE_DIR}/../daemond/group
                ${CMAKE_CURRENT_SOURCE_DIR}/make_cpio_archive.sh
                COMMENT "Packing boot initrd boot_initrd.cpio"
        )
        # Cpio images are test inputs, not sources of any target, so build them as
        # part of the default build to guarantee they exist before ctest runs.
        add_custom_target(astia_boot_initrd ALL DEPENDS ${_fix_dir}/boot_initrd.cpio)

        add_custom_command(
                OUTPUT ${_fix_dir}/production_exit_initrd.cpio
                COMMAND ${CMAKE_COMMAND} -E make_directory
                ${_fix_dir}/production_exit_stage/sbin
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -static -Wl,-no-pie
                -Wl,-e,_start -Wl,-Ttext-segment=0x40000000
                -o ${_fix_dir}/init_exit
                $<TARGET_OBJECTS:astia_production_exit_init>
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/init_exit
                ${_fix_dir}/production_exit_stage/sbin/init
                COMMAND ${CMAKE_COMMAND} -E env ASTIA_CPIO=${ASTIA_CPIO}
                sh ${CMAKE_CURRENT_SOURCE_DIR}/make_cpio_archive.sh
                ${_fix_dir}/production_exit_stage
                ${_fix_dir}/production_exit_initrd.cpio
                sbin/init
                DEPENDS astia_production_exit_init
                ${CMAKE_CURRENT_SOURCE_DIR}/make_cpio_archive.sh
                COMMENT "Packing production init exit initrd")
        add_custom_target(astia_production_exit_initrd ALL
                DEPENDS ${_fix_dir}/production_exit_initrd.cpio)

        add_test(NAME astia_initrd_boot
                COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_boot_test.sh
                ${ASTIA_QEMU} $<TARGET_FILE:astiakrn_test>
                ${_fix_dir}/boot_initrd.cpio
                "mod: hello_initrd.o rc=42"
                "selftest: statx ok"
                "user thread exit pid="
                "spawn reaped pid=1 status=0 path=/sbin/init"
                "astia: libc ok"
        )
        set_tests_properties(astia_initrd_boot PROPERTIES TIMEOUT 60)

        add_test(NAME astia_production_init_exit
                COMMAND ${CMAKE_COMMAND} -E env
                QEMU=${ASTIA_QEMU}
                ASTIA_KERNEL=$<TARGET_FILE:astiakrn>
                ASTIA_INITRD=${_fix_dir}/production_exit_initrd.cpio
                sh ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_init_exit.sh)
        set_tests_properties(astia_production_init_exit PROPERTIES TIMEOUT 30
                FIXTURES_REQUIRED astia-qemu-artifacts)

        # Native PID-1 split: init only bootstraps/waits for daemond; daemond owns
        # the fixed-capacity, non-shell rc file.  Keep this as a separate image
        # from init_user, whose purpose is the raw syscall ABI regression.
        function(astia_add_native_program name source extra_include)
            set(_native_object ${_fix_dir}/${name}.o)
            if (extra_include)
                set(_native_extra_include -I${extra_include})
            else ()
                set(_native_extra_include)
            endif ()
            add_custom_command(
                    OUTPUT ${_fix_dir}/${name}
                    COMMAND ${CMAKE_C_COMPILER} -c -ffreestanding -fno-pic -fno-pie
                    -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone
                    -fno-builtin -I${ASTIA_KERNEL_SOURCE_DIR}/../libc/include
                    -I${ASTIA_KERNEL_SOURCE_DIR}/include ${_native_extra_include}
                    -o ${_native_object} ${source}
                    COMMAND ${CMAKE_C_COMPILER} -nostdlib -static -Wl,-no-pie
                    -Wl,-e,_start -Wl,-Ttext-segment=0x40000000
                    -o ${_fix_dir}/${name} ${_fix_dir}/crt0.o ${_native_object}
                    $<TARGET_FILE:astia_userspace_libc> -lgcc
                    DEPENDS ${source}
                    ${ASTIA_KERNEL_SOURCE_DIR}/../daemond/daemond.h
                    ${ASTIA_KERNEL_SOURCE_DIR}/../libc/include/daemon_protocol.h
                    ${ASTIA_KERNEL_SOURCE_DIR}/../coreutils/src/common.h
                    ${_fix_dir}/crt0.o astia_userspace_libc
                    COMMENT "Building native ${name} image")
        endfunction()
        astia_add_native_program(native_init
                ${ASTIA_KERNEL_SOURCE_DIR}/../init/init.c "")
        astia_add_native_program(native_daemond
                ${ASTIA_KERNEL_SOURCE_DIR}/../daemond/daemond.c "")
        astia_add_native_program(native_daemondctl
                ${ASTIA_KERNEL_SOURCE_DIR}/../coreutils/src/daemonctl.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../coreutils/src)
        astia_add_native_program(native_ps
                ${ASTIA_KERNEL_SOURCE_DIR}/../coreutils/src/ps.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../coreutils/src)
        astia_add_native_program(native_true
                ${ASTIA_KERNEL_SOURCE_DIR}/../coreutils/src/true.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../coreutils/src)
        add_custom_target(astia_native_programs
                DEPENDS ${_fix_dir}/native_init ${_fix_dir}/native_daemond
                ${_fix_dir}/native_daemondctl ${_fix_dir}/native_ps ${_fix_dir}/native_true)
        add_custom_command(
                OUTPUT ${_fix_dir}/native_boot_initrd.cpio
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/native_stage/sbin
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/native_stage/bin
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/native_stage/etc
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/native_init ${_fix_dir}/native_stage/sbin/init
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/native_daemond ${_fix_dir}/native_stage/sbin/daemond
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/native_daemondctl ${_fix_dir}/native_stage/bin/daemonctl
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/native_ps ${_fix_dir}/native_stage/bin/ps
                COMMAND ${CMAKE_COMMAND} -E copy ${ASTIA_KERNEL_SOURCE_DIR}/../daemond/rc ${_fix_dir}/native_stage/etc/rc
                COMMAND ${CMAKE_COMMAND} -E copy ${_init_libc_image} ${_fix_dir}/native_stage/bin/init_libc
                COMMAND ${CMAKE_COMMAND} -E env ASTIA_CPIO=${ASTIA_CPIO}
                sh ${CMAKE_CURRENT_SOURCE_DIR}/make_cpio_archive.sh
                ${_fix_dir}/native_stage ${_fix_dir}/native_boot_initrd.cpio
                sbin/init sbin/daemond bin/daemonctl bin/ps bin/init_libc etc/rc
                DEPENDS ${_fix_dir}/native_init ${_fix_dir}/native_daemond
                ${_fix_dir}/native_daemondctl ${_fix_dir}/native_ps
                ${_init_libc_image} ${ASTIA_KERNEL_SOURCE_DIR}/../daemond/rc
                ${CMAKE_CURRENT_SOURCE_DIR}/make_cpio_archive.sh
                COMMENT "Packing native init/daemond initrd")
        add_test(NAME astia_native_daemond_boot
                COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_boot_test.sh
                ${ASTIA_QEMU} $<TARGET_FILE:astiakrn_test>
                ${_fix_dir}/native_boot_initrd.cpio
                "init: daemond started"
                "daemond: clean shutdown"
                "init: clean shutdown")
        set_tests_properties(astia_native_daemond_boot PROPERTIES TIMEOUT 60)

        add_custom_command(
                OUTPUT ${_fix_dir}/native_service_boot_initrd.cpio
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/native_service_stage/sbin
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/native_service_stage/bin
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/native_service_stage/etc
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/native_init ${_fix_dir}/native_service_stage/sbin/init
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/native_daemond ${_fix_dir}/native_service_stage/sbin/daemond
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/native_true ${_fix_dir}/native_service_stage/bin/true
                COMMAND ${CMAKE_COMMAND} -E copy ${_init_libc_image} ${_fix_dir}/native_service_stage/bin/init_libc
                COMMAND ${CMAKE_COMMAND} -E copy ${ASTIA_KERNEL_SOURCE_DIR}/../daemond/service.rc ${_fix_dir}/native_service_stage/etc/rc
                COMMAND ${CMAKE_COMMAND} -E env ASTIA_CPIO=${ASTIA_CPIO}
                sh ${CMAKE_CURRENT_SOURCE_DIR}/make_cpio_archive.sh
                ${_fix_dir}/native_service_stage ${_fix_dir}/native_service_boot_initrd.cpio
                sbin/init sbin/daemond bin/true bin/init_libc etc/rc
                DEPENDS ${_fix_dir}/native_init ${_fix_dir}/native_daemond
                ${_fix_dir}/native_true ${_init_libc_image}
                ${ASTIA_KERNEL_SOURCE_DIR}/../daemond/service.rc
                ${CMAKE_CURRENT_SOURCE_DIR}/make_cpio_archive.sh
                COMMENT "Packing native service supervision initrd")
        add_test(NAME astia_native_daemond_service
                COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_boot_test.sh
                ${ASTIA_QEMU} $<TARGET_FILE:astiakrn_test>
                ${_fix_dir}/native_service_boot_initrd.cpio
                "init: daemond started"
                "daemond: reaped service pid="
                "daemond: clean shutdown"
                "init: clean shutdown")
        set_tests_properties(astia_native_daemond_service PROPERTIES TIMEOUT 60)
        add_custom_target(astia_native_service_boot_initrd ALL
                DEPENDS ${_fix_dir}/native_service_boot_initrd.cpio)

        add_custom_command(
                OUTPUT ${_fix_dir}/native_failure_boot_initrd.cpio
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/native_failure_stage/sbin
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/native_failure_stage/bin
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/native_failure_stage/etc
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/native_init ${_fix_dir}/native_failure_stage/sbin/init
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/native_daemond ${_fix_dir}/native_failure_stage/sbin/daemond
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/native_daemondctl ${_fix_dir}/native_failure_stage/bin/daemonctl
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/native_ps ${_fix_dir}/native_failure_stage/bin/ps
                COMMAND ${CMAKE_COMMAND} -E copy ${_init_libc_image} ${_fix_dir}/native_failure_stage/bin/init_libc
                COMMAND ${CMAKE_COMMAND} -E copy ${ASTIA_KERNEL_SOURCE_DIR}/../daemond/failure.rc ${_fix_dir}/native_failure_stage/etc/rc
                COMMAND ${CMAKE_COMMAND} -E env ASTIA_CPIO=${ASTIA_CPIO}
                sh ${CMAKE_CURRENT_SOURCE_DIR}/make_cpio_archive.sh
                ${_fix_dir}/native_failure_stage ${_fix_dir}/native_failure_boot_initrd.cpio
                sbin/init sbin/daemond bin/daemonctl bin/ps bin/init_libc etc/rc
                DEPENDS ${_fix_dir}/native_init ${_fix_dir}/native_daemond
                ${_fix_dir}/native_daemondctl ${_fix_dir}/native_ps
                ${_init_libc_image} ${ASTIA_KERNEL_SOURCE_DIR}/../daemond/failure.rc
                ${CMAKE_CURRENT_SOURCE_DIR}/make_cpio_archive.sh
                COMMENT "Packing failing native daemond initrd")
        add_test(NAME astia_native_daemond_failure
                COMMAND env
                ASTIA_EXPECT_QEMU_STATUS=3
                ASTIA_REQUIRE_SELFTEST=0
                ASTIA_FORBID_PATTERN="core: selftest ok"
                sh ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_boot_test.sh
                ${ASTIA_QEMU} $<TARGET_FILE:astiakrn_test>
                ${_fix_dir}/native_failure_boot_initrd.cpio
                "daemond: malformed service record"
                "init: daemond failed")
        set_tests_properties(astia_native_daemond_failure PROPERTIES TIMEOUT 60)
        add_custom_target(astia_native_failure_boot_initrd ALL
                DEPENDS ${_fix_dir}/native_failure_boot_initrd.cpio)

        add_custom_target(astia_native_initrd_fixtures ALL
                DEPENDS ${_fix_dir}/native_boot_initrd.cpio
                ${_fix_dir}/native_service_boot_initrd.cpio
                ${_fix_dir}/native_failure_boot_initrd.cpio)

        if (GRUB_FILE AND GRUB_MKRESCUE AND XORRISO)
            add_custom_target(astia_grub_iso ALL
                    COMMAND BUILD=${CMAKE_BINARY_DIR}/grub ISO=${CMAKE_BINARY_DIR}/astia-grub.iso
                            KERNEL_TARGET=astiakrn_test
                            ${ASTIA_KERNEL_SOURCE_DIR}/scripts/make-grub-iso.sh
                    BYPRODUCTS ${CMAKE_BINARY_DIR}/astia-grub.iso
                    DEPENDS astiakrn_test
                    WORKING_DIRECTORY ${ASTIA_KERNEL_SOURCE_DIR}
                    COMMENT "Building GRUB Multiboot2 ISO")
            add_test(NAME astia_grub_smp_boot
                    COMMAND env ASTIA_GRUB_EXPECT_AP_WORK=1 ASTIA_GRUB_EXPECT_APIC_TOPOLOGY=1
                            sh ${ASTIA_KERNEL_SOURCE_DIR}/scripts/test-grub-smp.sh
                    ${CMAKE_BINARY_DIR}/astia-grub.iso)
            set_tests_properties(astia_grub_smp_boot PROPERTIES TIMEOUT 30)
            add_test(NAME astia_grub_framebuffer_boot
                    COMMAND env ASTIA_GRUB_EXPECT_FRAMEBUFFER=ready sh ${ASTIA_KERNEL_SOURCE_DIR}/scripts/test-grub-smp.sh
                    ${CMAKE_BINARY_DIR}/astia-grub.iso)
            set_tests_properties(astia_grub_framebuffer_boot PROPERTIES TIMEOUT 30)
            add_test(NAME astia_grub_smp4_boot
                    COMMAND env ASTIA_SMP=4 ASTIA_GRUB_EXPECT_AP_WORK=1
                            ASTIA_GRUB_EXPECT_APIC_TOPOLOGY=1
                            sh ${ASTIA_KERNEL_SOURCE_DIR}/scripts/test-grub-smp.sh
                            ${CMAKE_BINARY_DIR}/astia-grub.iso)
            set_tests_properties(astia_grub_smp4_boot PROPERTIES TIMEOUT 30)
        endif ()
    else ()
        message(STATUS "astia: qemu-system-x86_64 not found; skipping astia_kernel_boot")
    endif ()
endif ()

astia_append_test_label("kernel;deterministic")
foreach (_astia_boot_test IN ITEMS
        astia_kernel_boot astia_fs_registry_boot astia_kernel_lifecycle_smp2
        astia_kernel_smp_boot astia_kernel_scheduler_smp2 astia_initrd_boot
        astia_production_init_exit
        astia_native_daemond_boot astia_native_daemond_service
        astia_native_daemond_failure)
    if(TEST ${_astia_boot_test})
        astia_add_test_label(${_astia_boot_test} "boot;qemu")
        set_tests_properties(${_astia_boot_test}
                PROPERTIES RESOURCE_LOCK astia-qemu RUN_SERIAL TRUE)
    endif()
endforeach()
foreach (_astia_grub_test IN ITEMS
        astia_grub_smp_boot astia_grub_framebuffer_boot astia_grub_smp4_boot)
    if(TEST ${_astia_grub_test})
        astia_add_test_label(${_astia_grub_test} "boot;grub;qemu")
        set_tests_properties(${_astia_grub_test}
                PROPERTIES RESOURCE_LOCK astia-qemu RUN_SERIAL TRUE)
    endif()
endforeach()
foreach (_astia_vga_test IN ITEMS astia_pvh_test astia_framebuffer_test astia_virtual_tty_test)
    if(TEST ${_astia_vga_test})
        astia_add_test_label(${_astia_vga_test} "vga;pvh;ttv0")
    endif()
endforeach()
foreach (_astia_ext4_test IN ITEMS astia_ext4_test astia_ext4_real_test astia_ext4_vfs_test)
    if(TEST ${_astia_ext4_test})
        astia_add_test_label(${_astia_ext4_test} "ext4;filesystem")
    endif()
endforeach()
