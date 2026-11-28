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
                ${ASTIA_KERNEL_SOURCE_DIR}/../tools/man/test_syscall_layout.py)
set_tests_properties(astia_man_generator_layout PROPERTIES
        LABELS "docs;deterministic")
add_test(NAME astia_syscall_abi_migration
        COMMAND ${Python3_EXECUTABLE}
                ${ASTIA_KERNEL_SOURCE_DIR}/../tools/man/test_syscall_layout.py --abi-only)
set_tests_properties(astia_syscall_abi_migration PROPERTIES
        LABELS "docs;abi;deterministic")
add_test(NAME astia_man_generator_unit
        COMMAND ${Python3_EXECUTABLE}
                ${ASTIA_KERNEL_SOURCE_DIR}/../tools/man/test_generate.py)
set_tests_properties(astia_man_generator_unit PROPERTIES
        LABELS "docs;deterministic")

find_package(Threads REQUIRED)
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
        support/cpu_relax_host.cpp
        support/interrupts_host.cpp
        support/platform_host.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/io/request_queue.cpp
)
set_source_files_properties(support/main.cpp
        PROPERTIES COMPILE_OPTIONS "-fno-threadsafe-statics")
set_source_files_properties(
        ${ASTIA_KERNEL_SOURCE_DIR}/src/io/request_queue.cpp
        PROPERTIES COMPILE_OPTIONS "-fno-sanitize=vptr")
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
        ${ASTIA_KERNEL_SOURCE_DIR}/src
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
    if (ASTIA_HOST_TEST_LAUNCHER)
        add_test(NAME ${name} COMMAND ${ASTIA_HOST_TEST_LAUNCHER}
                $<TARGET_FILE:${name}>)
    else ()
        add_test(NAME ${name} COMMAND ${name})
    endif ()
    astia_add_test_label(${name} "sanitizer-host")
endfunction()

# --- Pure logic: no hardware, no fake memory needed. ------------------------
astia_add_host_test(acpi_host_test acpi_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/acpi/madt.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/acpi/tables.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/acpi/aml/core.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/acpi/aml/decode.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/acpi/aml/eval.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/acpi/aml/namespace.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/acpi/aml/parse.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/acpi/power.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/core/shutdown.cpp)
target_include_directories(acpi_host_test PRIVATE
        ${ASTIA_KERNEL_SOURCE_DIR}/src/acpi)
target_compile_definitions(acpi_host_test PRIVATE ASTIA_HOST_TEST=1)
astia_add_test_label(acpi_host_test "acpi;host;deterministic")
astia_add_host_test(astia_shutdown_host_test shutdown_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/core/shutdown.cpp)
target_compile_definitions(astia_shutdown_host_test PRIVATE ASTIA_HOST_TEST=1)
astia_add_test_label(astia_shutdown_host_test "core;shutdown;host;deterministic")
astia_add_host_test(astia_memory_map_test memory_map_test.cpp)
astia_add_host_test(astia_pvh_test pvh_test.cpp ${ASTIA_KERNEL_SOURCE_DIR}/src/boot/virt/pvh.cpp)
astia_add_host_test(astia_multiboot2_test multiboot2_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/boot/virt/multiboot2.cpp)
astia_add_host_test(astia_boot_init_test boot_init_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/boot/init.cpp)
astia_add_host_test(astia_aarch64_fdt_test aarch64_fdt_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/boot/aarch64-virt/fdt.cpp)
astia_add_host_test(astia_aarch64_page_map_test
        aarch64_page_map_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/arch/aarch64/page_map.cpp)
target_include_directories(astia_aarch64_page_map_test PRIVATE
        ${ASTIA_KERNEL_SOURCE_DIR}/src/arch/aarch64)
target_compile_definitions(astia_aarch64_page_map_test PRIVATE ASTIA_HOST_TEST=1)
astia_add_host_test(astia_csprng_test
        csprng_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/crypto/csprng.cpp)
astia_add_host_test(astia_entropy_pool_test
        entropy_pool_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/crypto/entropy_pool.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/crypto/csprng.cpp)
astia_add_test_label(astia_entropy_pool_test "crypto;host;deterministic")
astia_add_host_test(astia_random_seed_test
        random_seed_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/crypto/csprng.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/crypto/entropy_pool.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/crypto/random.cpp
        support/entropy_weak_host.cpp)
astia_add_test_label(astia_random_seed_test "crypto;host;deterministic")
astia_add_host_test(astia_aarch64_exception_frame_test
        aarch64_exception_frame_test.cpp)
target_include_directories(astia_aarch64_exception_frame_test PRIVATE
        ${ASTIA_KERNEL_SOURCE_DIR}/include/arch/aarch64)
astia_add_host_test(astia_aarch64_gic_test aarch64_gic_test.cpp)
target_include_directories(astia_aarch64_gic_test PRIVATE
        ${ASTIA_KERNEL_SOURCE_DIR}/src/arch/aarch64)
astia_add_host_test(astia_aarch64_irq_test aarch64_irq_test.cpp)
target_include_directories(astia_aarch64_irq_test PRIVATE
        ${ASTIA_KERNEL_SOURCE_DIR}/src/arch/aarch64)
astia_add_test_label(astia_aarch64_irq_test "aarch64;host;deterministic")
astia_add_host_test(astia_aarch64_scheduler_test aarch64_scheduler_test.cpp)
target_include_directories(astia_aarch64_scheduler_test PRIVATE
        ${ASTIA_KERNEL_SOURCE_DIR}/src/boot/aarch64-virt)
astia_add_test_label(astia_aarch64_scheduler_test "aarch64;host;deterministic")
astia_add_host_test(astia_run_queue_test run_queue_test.cpp)
astia_add_test_label(astia_run_queue_test "host;deterministic")
add_test(NAME astia_init_launch_shape
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/init_launch_shape.py
                ${ASTIA_KERNEL_SOURCE_DIR})
astia_add_test_label(astia_init_launch_shape "init;deterministic")
add_test(NAME astia_normalize_init_log
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_normalize_init_log.py)
astia_add_test_label(astia_normalize_init_log "deterministic;init")

# error_t / optional<T>: header-only carriers for fallible kernel operations.
astia_add_host_test(astia_optional_test optional_test.cpp)
astia_add_host_test(astia_completion_test
        completion_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/core/resume_queue.cpp
        support/resume_queue_host.cpp)
astia_add_host_test(astia_atomic_test atomic_test.cpp)
astia_add_test_label(astia_atomic_test "deterministic;core")
astia_add_host_test(astia_hook_test hook_test.cpp)
astia_add_host_test(astia_dl_test dl_test.cpp)
astia_add_host_test(astia_dl_symcount_test dl_symcount_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/../dl/symcount.c)
astia_add_test_label(astia_dl_symcount_test "dl;host;deterministic")
astia_add_host_test(astia_dl_mode_test dl_mode_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/../dl/mode.c)
astia_add_test_label(astia_dl_mode_test "dl;host;deterministic")
set(_dl_ld_host_sources
        dl_unload_host.c
        ${ASTIA_KERNEL_SOURCE_DIR}/../dl/aio_wait.c
        ${ASTIA_KERNEL_SOURCE_DIR}/../dl/error.c
        ${ASTIA_KERNEL_SOURCE_DIR}/../dl/mode.c
        ${ASTIA_KERNEL_SOURCE_DIR}/../dl/symcount.c)
set(_dl_unload_includes
        "${ASTIA_KERNEL_SOURCE_DIR}/../dl"
        "${ASTIA_KERNEL_SOURCE_DIR}/../dl/arch/x86_64"
        "${ASTIA_KERNEL_SOURCE_DIR}/../libc/include")
set_source_files_properties(dl_unload_host.c PROPERTIES
        INCLUDE_DIRECTORIES "${_dl_unload_includes}")
astia_add_host_test(astia_dl_unload_test dl_unload_test.cpp ${_dl_ld_host_sources})
astia_add_test_label(astia_dl_unload_test "dl;host;deterministic")
astia_add_host_test(astia_dl_iterate_phdr_test
        dl_iterate_phdr_test.cpp ${_dl_ld_host_sources})
astia_add_test_label(astia_dl_iterate_phdr_test "dl;host;deterministic")
astia_add_host_test(astia_dl_error_test dl_error_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/../dl/error.c)
target_include_directories(astia_dl_error_test PRIVATE
        ${ASTIA_KERNEL_SOURCE_DIR}/../dl)
target_link_libraries(astia_dl_error_test PRIVATE Threads::Threads)
astia_add_test_label(astia_dl_error_test "dl;host;deterministic")
astia_add_host_test(astia_dl_aio_wait_test dl_aio_wait_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/../dl/aio_wait.c)
astia_add_test_label(astia_dl_aio_wait_test "dl;host;deterministic")
astia_add_host_test(astia_task_test
        task_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/core/resume_queue.cpp
        support/resume_queue_host.cpp
)
astia_add_host_test(astia_thread_park_test
        thread_park_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/thread.cpp
)
target_compile_options(astia_thread_park_test PRIVATE
        -ffunction-sections -fdata-sections)
target_link_options(astia_thread_park_test PRIVATE -Wl,--gc-sections)
astia_add_test_label(astia_thread_park_test "deterministic;proc")
astia_add_host_test(astia_fs_async_test
        fs_async_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/fs/fs_io.cpp
        support/file_host.cpp
        support/device_ready_host.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/core/resume_queue.cpp
        support/resume_queue_host.cpp
)
target_compile_options(astia_fs_async_test PRIVATE -fno-sanitize=vptr)
target_link_options(astia_fs_async_test PRIVATE -fno-sanitize=vptr)
astia_add_host_test(astia_pipe_test
        pipe_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/ipc/pipe.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/fs/fs_io.cpp
        support/file_host.cpp
        support/device_ready_host.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/core/resume_queue.cpp
        support/resume_queue_host.cpp
)
target_compile_options(astia_pipe_test PRIVATE -fno-sanitize=vptr)
target_link_options(astia_pipe_test PRIVATE -fno-sanitize=vptr)
astia_add_test_label(astia_pipe_test "deterministic;ipc")
astia_add_host_test(astia_mapping_test mapping_test.cpp)
astia_add_host_test(astia_mmap_chunk_test mmap_chunk_test.cpp)
astia_add_host_test(astia_user_layout_test user_layout_test.cpp)
astia_add_host_test(astia_common_test common_test.cpp)
astia_add_host_test(astia_core_name_test
        core_name_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/core/name.cpp)
astia_add_test_label(astia_core_name_test "deterministic;core")
astia_add_host_test(astia_utsname_test utsname_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/arch/x86_64/identity.cpp)
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
astia_add_host_test(astia_pci_mmio_policy_test pci_mmio_policy_test.cpp)
astia_add_host_test(astia_pci_mmio_owner_test
        pci_mmio_owner_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/arch/x86_64/pci_mmio_owner.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/arch/x86_64/layout.cpp)
astia_add_host_test(astia_platform_test platform_test.cpp)
astia_add_host_test(astia_resource_limits_test resource_limits_test.cpp)
astia_add_host_test(astia_regs_test regs_test.cpp)
astia_add_host_test(astia_capabilities_test capabilities_test.cpp)
astia_add_host_test(astia_capabilities_header_test capabilities_header_test.cpp)
astia_add_host_test(astia_io_header_test io_header_test.cpp)
astia_add_host_test(astia_io_request_test io_request_test.cpp)
astia_add_host_test(astia_irq_header_test irq_header_test.cpp)
astia_add_host_test(astia_interrupts_guard_test interrupts_guard_test.cpp)
astia_add_test_label(astia_interrupts_guard_test "kernel;deterministic")
astia_add_host_test(astia_endian_test endian_test.cpp)
astia_add_host_test(astia_time_test
        time_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/sys/time.cpp)
target_compile_options(astia_time_test PRIVATE -ffunction-sections -fdata-sections)
target_link_options(astia_time_test PRIVATE -Wl,--gc-sections)
astia_add_host_test(astia_clock_syscalls_test clock_syscalls_test.cpp)
target_compile_options(astia_clock_syscalls_test PRIVATE
        -ffunction-sections -fdata-sections)
target_link_options(astia_clock_syscalls_test PRIVATE -Wl,--gc-sections)
astia_add_test_label(astia_clock_syscalls_test "deterministic;sys")
astia_add_host_test(astia_syscall_handler_prototype_test
        syscall_handler_prototype_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/core/resume_queue.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/sys/syscall.cpp
        support/resume_queue_host.cpp)
target_compile_options(astia_syscall_handler_prototype_test PRIVATE
        -ffunction-sections -fdata-sections)
target_link_options(astia_syscall_handler_prototype_test PRIVATE
        -Wl,--gc-sections)
astia_add_host_test(astia_power_syscall_test
        power_syscall_test.cpp
        power_syscall_host.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/syscalls/power.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/sys/dispatch.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/sys/syscall.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/core/resume_queue.cpp
        support/resume_queue_host.cpp)
target_compile_options(astia_power_syscall_test PRIVATE
        -ffunction-sections -fdata-sections)
target_compile_definitions(astia_power_syscall_test PRIVATE
        ASTIA_MODULE_HOST_TEST=1)
target_link_options(astia_power_syscall_test PRIVATE
        "-Wl,-no-pie" "-Wl,--gc-sections")
astia_add_host_test(astia_console_test
        console_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/console.cpp)
astia_add_host_test(astia_ownership_test ownership_test.cpp)
astia_add_host_test(astia_signal_test signal_test.cpp)
target_compile_definitions(astia_signal_test PRIVATE ASTIA_SIGNAL_HOST_TEST=1)
astia_add_host_test(astia_aio_test aio_test.cpp)
astia_add_host_test(astia_aio_ring_metadata_test aio_ring_metadata_test.cpp)
file(GLOB ASTIA_AIO_CORE_HANDLER_TEST_SOURCES CONFIGURE_DEPENDS
        ${ASTIA_KERNEL_SOURCE_DIR}/src/io/aio/core/*.cpp)
file(GLOB ASTIA_AIO_FS_HANDLER_TEST_SOURCES CONFIGURE_DEPENDS
        ${ASTIA_KERNEL_SOURCE_DIR}/src/fs/aio/*.cpp)
file(GLOB ASTIA_AIO_NET_HANDLER_TEST_SOURCES CONFIGURE_DEPENDS
        ${ASTIA_KERNEL_SOURCE_DIR}/src/net/aio/*.cpp)
file(GLOB ASTIA_AIO_READINESS_HANDLER_TEST_SOURCES CONFIGURE_DEPENDS
        ${ASTIA_KERNEL_SOURCE_DIR}/src/io/readiness/aio/*.cpp)
set(ASTIA_AIO_HANDLER_TEST_SOURCES
        ${ASTIA_AIO_CORE_HANDLER_TEST_SOURCES}
        ${ASTIA_AIO_FS_HANDLER_TEST_SOURCES}
        ${ASTIA_AIO_NET_HANDLER_TEST_SOURCES}
        ${ASTIA_AIO_READINESS_HANDLER_TEST_SOURCES})
set(ASTIA_AIO_REGISTRY_TEST_SOURCES
        aio_registry_test_support.cpp
        support/file_host.cpp
        support/interrupts_host.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/io/aio/dispatch.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/io/aio/registry.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/io/aio/runner.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/fs/fs_io.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/ipc/unix_control.cpp
        ${ASTIA_AIO_HANDLER_TEST_SOURCES})
astia_add_host_test(astia_aio_static_handlers_test
        aio_static_handlers_test.cpp ${ASTIA_AIO_REGISTRY_TEST_SOURCES})
astia_add_host_test(astia_aio_filesystem_test
        aio_filesystem_test.cpp ${ASTIA_AIO_REGISTRY_TEST_SOURCES})
astia_add_host_test(astia_aio_readiness_test
        aio_readiness_test.cpp ${ASTIA_AIO_REGISTRY_TEST_SOURCES})
astia_add_host_test(astia_aio_socket_test
        aio_socket_test.cpp ${ASTIA_AIO_REGISTRY_TEST_SOURCES})
astia_add_host_test(astia_unix_socket_accept_test
        unix_socket_accept_test.cpp ${ASTIA_AIO_REGISTRY_TEST_SOURCES}
        ${ASTIA_KERNEL_SOURCE_DIR}/src/ipc/unix_socket.cpp)
target_compile_definitions(astia_unix_socket_accept_test PRIVATE
        ASTIA_AIO_TEST_REAL_UNIX_SOCKETS=1)
astia_add_host_test(astia_unix_control_test
        unix_control_test.cpp
        ${ASTIA_AIO_REGISTRY_TEST_SOURCES})
foreach(_astia_aio_test IN ITEMS astia_aio_static_handlers_test astia_aio_filesystem_test
        astia_aio_readiness_test astia_aio_socket_test astia_unix_socket_accept_test)
    target_compile_options(${_astia_aio_test} PRIVATE -fno-pie)
    target_link_options(${_astia_aio_test} PRIVATE
            "-Wl,-no-pie"
            "-Wl,--defsym=__kernel_start=0x08000000"
            "-Wl,--defsym=__kernel_end=0x08002000")
endforeach()
add_test(NAME astia_aio_handler_ownership
        COMMAND ${CMAKE_COMMAND}
        -DASTIA_KERNEL_SOURCE_DIR=${ASTIA_KERNEL_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/aio_handler_ownership_test.cmake)
set_tests_properties(astia_aio_handler_ownership
        PROPERTIES LABELS "kernel;deterministic")
astia_add_host_test(astia_socket_abi_test socket_abi_test.cpp)
astia_add_host_test(astia_fd_table_test
        fd_table_test.cpp
        support/file_host.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/fd_table.cpp)
target_link_libraries(astia_fd_table_test PRIVATE Threads::Threads)
astia_add_test_label(astia_fd_table_test "deterministic;proc")
astia_add_host_test(astia_process_group_test process_group_test.cpp)
astia_add_host_test(astia_cwd_context_test
        cwd_context_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/core/resume_queue.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/cwd_context.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/fs/path_service.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/mm/heap.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/mm/frame_allocator.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/mm/placement_new.cpp
        support/arena.cpp
        support/resume_queue_host.cpp)
target_compile_options(astia_cwd_context_test PRIVATE -ffunction-sections -fdata-sections -fno-pie)
target_link_options(astia_cwd_context_test PRIVATE
        "-Wl,-no-pie"
        "-Wl,--defsym=__kernel_start=0x08000000"
        "-Wl,--defsym=__kernel_end=0x08002000")
target_compile_definitions(astia_cwd_context_test PRIVATE ASTIA_HOST_TEST=1)
target_link_options(astia_cwd_context_test PRIVATE -Wl,--gc-sections)
astia_add_host_test(astia_tty_test
        tty_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/chr/tty.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/device.cpp
)
target_compile_definitions(astia_tty_test PRIVATE ASTIA_TTY_HOST_TEST=1)
target_include_directories(astia_tty_test PRIVATE ${ASTIA_KERNEL_SOURCE_DIR}/src/dev)
astia_add_host_test(astia_uart16550_test
        uart16550_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/chr/tty/uart16550.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/chr/tty.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/device.cpp
)
target_compile_options(astia_uart16550_test PRIVATE -ffunction-sections -fdata-sections)
target_compile_definitions(astia_uart16550_test PRIVATE ASTIA_UART16550_HOST_TEST=1)
target_link_options(astia_uart16550_test PRIVATE -Wl,--gc-sections)
target_include_directories(astia_uart16550_test PRIVATE ${ASTIA_KERNEL_SOURCE_DIR}/src/dev)
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
        ${ASTIA_KERNEL_SOURCE_DIR}/src/crypto/csprng.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/crypto/entropy_pool.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/crypto/random.cpp
        support/entropy_host.cpp
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
        ${ASTIA_KERNEL_SOURCE_DIR}/src/fs/fs_io.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/core/resume_queue.cpp
        support/device_ready_host.cpp
        support/resume_queue_host.cpp)
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
        ${ASTIA_KERNEL_SOURCE_DIR}/src/hw/pci.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/hw/driver.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/device.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/core/resume_queue.cpp
        support/resume_queue_host.cpp)
target_include_directories(astia_pci_core_test PRIVATE ${ASTIA_KERNEL_SOURCE_DIR}/src/dev)

astia_add_host_test(astia_pci_test
        pci_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/hw/pci.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/hw/driver.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/device.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/hw/mmio.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/core/resume_queue.cpp)
target_compile_definitions(astia_pci_test PRIVATE ASTIA_HOST_TEST=1)
target_include_directories(astia_pci_test PRIVATE ${ASTIA_KERNEL_SOURCE_DIR}/src/dev)

astia_add_host_test(astia_panic_report_test
        panic_report_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dbg/panic_report.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/arch/x86_64/panic_format.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/arch/aarch64/panic_format.cpp)
target_include_directories(astia_panic_report_test PRIVATE
        ${ASTIA_KERNEL_SOURCE_DIR}/src/arch/x86_64
        ${ASTIA_KERNEL_SOURCE_DIR}/src/arch/aarch64
        ${ASTIA_KERNEL_SOURCE_DIR}/include/arch/x86_64
        ${ASTIA_KERNEL_SOURCE_DIR}/include/arch/aarch64)

astia_add_host_test(astia_driver_manager_test
        driver_manager_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/hw/driver.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/device.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/core/resume_queue.cpp
        support/resume_queue_host.cpp)
target_include_directories(astia_driver_manager_test PRIVATE ${ASTIA_KERNEL_SOURCE_DIR}/src/dev)

astia_add_host_test(astia_net_test
        net_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/net/net.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/net/neighbour.cpp
        support/timer_host.cpp)
target_compile_definitions(astia_net_test PRIVATE ASTIA_HOST_TEST=1)
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

if (DEFINED ASTIA_AARCH64_VIRT_CXX_COMPILER AND
        ASTIA_AARCH64_VIRT_CXX_COMPILER)
    set(ASTIA_ARCH_MATRIX_AARCH64_CXX
            "${ASTIA_AARCH64_VIRT_CXX_COMPILER}")
else ()
    find_program(ASTIA_ARCH_MATRIX_AARCH64_CXX
            NAMES aarch64-linux-gnu-g++ aarch64-none-elf-g++)
endif ()
if (NOT ASTIA_ARCH_MATRIX_AARCH64_CXX)
    set(ASTIA_ARCH_MATRIX_AARCH64_CXX "")
endif ()
add_test(NAME astia_arch_include_boundaries
        COMMAND ${CMAKE_COMMAND}
        -DASTIA_KERNEL_SOURCE_DIR=${ASTIA_KERNEL_SOURCE_DIR}
        -DASTIA_ARCH_INCLUDE_BOUNDARIES_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/arch_include_boundaries.py
        -DASTIA_PYTHON=${Python3_EXECUTABLE}
        -DASTIA_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DASTIA_AARCH64_CXX_COMPILER=${ASTIA_ARCH_MATRIX_AARCH64_CXX}
        -DASTIA_ARCH_INCLUDE_BOUNDARIES_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/arch-include-boundaries
        -P ${CMAKE_CURRENT_SOURCE_DIR}/arch_include_boundaries_test.cmake)
set_tests_properties(astia_arch_include_boundaries
        PROPERTIES LABELS "kernel;architecture;headers;deterministic"
        TIMEOUT 120)

add_test(NAME astia_atomic_builtin_boundary
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/atomic_builtin_boundary.py
                --root ${ASTIA_KERNEL_SOURCE_DIR}/..
                --self-test)
set_tests_properties(astia_atomic_builtin_boundary
        PROPERTIES LABELS "kernel;atomic;deterministic;core")

add_test(NAME astia_experimental_core_removal
        COMMAND ${CMAKE_COMMAND}
        -DASTIA_REPO_ROOT=${ASTIA_KERNEL_SOURCE_DIR}/..
        -P ${CMAKE_CURRENT_SOURCE_DIR}/experimental_core_removal_test.cmake)
set_tests_properties(astia_experimental_core_removal
        PROPERTIES LABELS "kernel;deterministic")

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

# ET_REL module fixtures: real compiler output with a task-shaped
# module_main ABI. All fixtures share the same freestanding flags and include
# path so a fixture never drifts from the module ABI it exercises.
set(ASTIA_ET_REL_FIXTURE_FLAGS
        -std=c++20 -c -ffreestanding -fno-pic -fno-pie -mcmodel=large
        -fno-exceptions -fno-rtti -fno-use-cxa-atexit -fno-stack-protector
        -fno-asynchronous-unwind-tables -fno-unwind-tables
        -DASTIA_MODULE_ET_REL=1
        -I${ASTIA_KERNEL_SOURCE_DIR}/include)

function(astia_add_et_rel_fixture name source)
    set(_obj ${_fix_dir}/${name}.o)
    add_custom_command(
            OUTPUT ${_obj}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}
            COMMAND ${CMAKE_CXX_COMPILER} ${ASTIA_ET_REL_FIXTURE_FLAGS}
                    -o ${_obj} ${source}
            DEPENDS ${source}
                    ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/core/task.hpp
                    ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/core/completion.hpp
                    ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/core/resume_queue.hpp
            COMMENT "Compiling ET_REL fixture ${name}"
            VERBATIM)
    astia_embed_blob(_${name}_cpp ${_obj} ${name}_blob)
    set(_${name}_cpp ${_${name}_cpp} PARENT_SCOPE)
endfunction()

astia_add_et_rel_fixture(hello_mod
        ${ASTIA_MODULES_SOURCE_DIR}/tests/hello_mod.cpp)
astia_add_et_rel_fixture(module_symbols
        ${CMAKE_CURRENT_SOURCE_DIR}/module_symbols.cpp)
astia_add_et_rel_fixture(module_priority_malformed
        ${CMAKE_CURRENT_SOURCE_DIR}/module_priority_malformed_fixture.cpp)
astia_add_et_rel_fixture(module_priority_overflow
        ${CMAKE_CURRENT_SOURCE_DIR}/module_priority_overflow_fixture.cpp)
foreach(_astia_priority_trailing_dot_fixture init fini)
    astia_add_et_rel_fixture(
            module_priority_${_astia_priority_trailing_dot_fixture}_trailing_dot
            ${CMAKE_CURRENT_SOURCE_DIR}/module_priority_${_astia_priority_trailing_dot_fixture}_trailing_dot_fixture.cpp)
endforeach()
astia_add_et_rel_fixture(module_lifecycle
        ${CMAKE_CURRENT_SOURCE_DIR}/module_lifecycle_fixture.cpp)
astia_add_et_rel_fixture(module_lifecycle_atexit
        ${CMAKE_CURRENT_SOURCE_DIR}/module_lifecycle_atexit_fixture.cpp)
astia_add_et_rel_fixture(module_lifecycle_async
        ${CMAKE_CURRENT_SOURCE_DIR}/module_lifecycle_async_fixture.cpp)
astia_add_et_rel_fixture(module_sync_entry
        ${CMAKE_CURRENT_SOURCE_DIR}/module_sync_entry_fixture.cpp)
foreach(_lifecycle_extra_fixture limit dso)
    astia_add_et_rel_fixture(module_lifecycle_${_lifecycle_extra_fixture}
            ${CMAKE_CURRENT_SOURCE_DIR}/module_lifecycle_${_lifecycle_extra_fixture}_fixture.cpp)
endforeach()

add_custom_command(
        OUTPUT ${_fix_dir}/module_lifecycle_no_fini.o
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -c -ffreestanding -fno-pic -fno-pie
        -mcmodel=large -fno-exceptions -fno-rtti -fno-use-cxa-atexit
        -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables
        -DASTIA_MODULE_ET_REL=1 -I${ASTIA_KERNEL_SOURCE_DIR}/include
        -o ${_fix_dir}/module_lifecycle_no_fini.o
        ${CMAKE_CURRENT_SOURCE_DIR}/module_lifecycle_no_fini_fixture.cpp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/module_lifecycle_no_fini_fixture.cpp
        COMMENT "Compiling ET_REL no-fini lifecycle fixture"
)
astia_embed_blob(_lifecycle_no_fini_fixture_cpp
        ${_fix_dir}/module_lifecycle_no_fini.o module_lifecycle_no_fini_blob)

add_custom_command(
        OUTPUT ${_fix_dir}/module_lifecycle_reloc32.o
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -c -ffreestanding -fno-pic -fno-pie
        -mcmodel=small -fno-exceptions -fno-rtti -fno-use-cxa-atexit
        -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables
        -DASTIA_MODULE_ET_REL=1 -I${ASTIA_KERNEL_SOURCE_DIR}/include
        -o ${_fix_dir}/module_lifecycle_reloc32.o
        ${CMAKE_CURRENT_SOURCE_DIR}/module_lifecycle_reloc32_fixture.cpp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/module_lifecycle_reloc32_fixture.cpp
        COMMENT "Compiling ET_REL 32-bit-absolute relocation fixture"
)
astia_embed_blob(_lifecycle_reloc32_fixture_cpp
        ${_fix_dir}/module_lifecycle_reloc32.o module_lifecycle_reloc32_blob)

add_custom_command(
        OUTPUT ${_fix_dir}/module_lifecycle_module_api.o
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -c -ffreestanding -fno-pic -fno-pie
        -mcmodel=large -fno-exceptions -fno-rtti -fno-use-cxa-atexit
        -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables
        -DASTIA_MODULE_ET_REL=1 -I${ASTIA_KERNEL_SOURCE_DIR}/include
        -o ${_fix_dir}/module_lifecycle_module_api.o
        ${CMAKE_CURRENT_SOURCE_DIR}/module_lifecycle_module_api_fixture.cpp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/module_lifecycle_module_api_fixture.cpp
        COMMENT "Compiling ET_REL module API lifecycle fixture"
)
astia_embed_blob(_lifecycle_module_api_fixture_cpp
        ${_fix_dir}/module_lifecycle_module_api.o module_lifecycle_module_api_blob)

add_custom_command(
        OUTPUT ${_fix_dir}/module_lifecycle_priority.o
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -c -ffreestanding -fno-pic -fno-pie
        -mcmodel=large -fno-exceptions -fno-rtti -fno-use-cxa-atexit
        -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables
        -DASTIA_MODULE_ET_REL=1 -I${ASTIA_KERNEL_SOURCE_DIR}/include
        -o ${_fix_dir}/module_lifecycle_priority.o
        ${CMAKE_CURRENT_SOURCE_DIR}/module_lifecycle_priority_fixture.cpp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/module_lifecycle_priority_fixture.cpp
        COMMENT "Compiling ET_REL lifecycle priority fixture"
)
astia_embed_blob(_lifecycle_priority_fixture_cpp
        ${_fix_dir}/module_lifecycle_priority.o module_lifecycle_priority_blob)

add_custom_command(
        OUTPUT ${_fix_dir}/module_lifecycle_wait.o
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -c -ffreestanding -fno-pic -fno-pie
        -mcmodel=large -fno-exceptions -fno-rtti -fno-use-cxa-atexit
        -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables
        -DASTIA_MODULE_ET_REL=1 -I${ASTIA_KERNEL_SOURCE_DIR}/include
        -o ${_fix_dir}/module_lifecycle_wait.o
        ${CMAKE_CURRENT_SOURCE_DIR}/module_lifecycle_wait_fixture.cpp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/module_lifecycle_wait_fixture.cpp
        COMMENT "Compiling ET_REL lifecycle wait fixture"
)
astia_embed_blob(_lifecycle_wait_fixture_cpp
        ${_fix_dir}/module_lifecycle_wait.o module_lifecycle_wait_blob)

add_custom_command(
        OUTPUT ${_fix_dir}/module_lifecycle_reentrant.o
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -c -ffreestanding -fno-pic -fno-pie
        -mcmodel=large -fno-exceptions -fno-rtti -fno-use-cxa-atexit
        -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables
        -DASTIA_MODULE_ET_REL=1 -I${ASTIA_KERNEL_SOURCE_DIR}/include
        -o ${_fix_dir}/module_lifecycle_reentrant.o
        ${CMAKE_CURRENT_SOURCE_DIR}/module_lifecycle_reentrant_fixture.cpp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/module_lifecycle_reentrant_fixture.cpp
        COMMENT "Compiling ET_REL lifecycle reentrant fixture"
)
astia_embed_blob(_lifecycle_reentrant_fixture_cpp
        ${_fix_dir}/module_lifecycle_reentrant.o module_lifecycle_reentrant_blob)

# Compile-only target: these fixtures are normally built by the custom
# ET_REL commands above, which do not appear in compile_commands.json.  The
# object library supplies valid compilation-database entries for clang-tidy
# without linking the fixture symbols into any test binary.
add_library(astia_module_lifecycle_fixture_compile_db OBJECT
        module_lifecycle_async_fixture.cpp
        module_lifecycle_reentrant_fixture.cpp
        module_lifecycle_wait_fixture.cpp
        module_lifecycle_module_api_fixture.cpp
        module_lifecycle_reloc32_fixture.cpp
        module_sync_entry_fixture.cpp)
target_compile_options(astia_module_lifecycle_fixture_compile_db PRIVATE
        -ffreestanding -fno-pic -fno-pie -mcmodel=large
        -fno-exceptions -fno-rtti -fno-use-cxa-atexit
        -DASTIA_MODULE_ET_REL=1
        -I${ASTIA_KERNEL_SOURCE_DIR}/include)

# Compile-only target: the dl boot fixtures are built by custom commands in
# the QEMU initrd rule, which do not appear in compile_commands.json.  Keep
# this at directory scope so the entries exist even when QEMU is not found;
# nothing links these objects.
add_library(astia_dl_boot_fixture_compile_db OBJECT
        fixture_gnu.c
        fixture_semihash.c
        fixture_ifunc.c
        fixture_dynamic_tls.c
        interp_gnu.c
        interp_relative_ro.c
        interp_relro.c
        interp_tpoff32.c
        interp_ifunc.c
        dl_fixture.c
        dl_fixture_dep.c
        dl_fixture_global.c
        dl_fixture_nd.c
        dl_fixture_user.c
        dl_dep_base.c
        dl_dep_b.c
        dl_dep_c.c
        dl_dep_a.c
        interp_dlopen.c
        interp_dyntls.c
        interp_copy.c
        interp_copy_lib.c
        fixture_versions.c
        interp_ver.c
        fixture_badver.c
        interp_badver.c)
target_include_directories(astia_dl_boot_fixture_compile_db PRIVATE
        ${ASTIA_KERNEL_SOURCE_DIR}/include
        ${ASTIA_KERNEL_SOURCE_DIR}/../libc/include)
target_compile_options(astia_dl_boot_fixture_compile_db PRIVATE
        -ffreestanding -fno-builtin -fno-stack-protector
        -fno-asynchronous-unwind-tables -mno-red-zone)

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
        ${_module_sync_entry_cpp}
        ${_fixture_dyn_cpp}
        ${_fixture_exec_cpp}
        ${ASTIA_KERNEL_SOURCE_DIR}/src/elf/elf.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/elf/loader/exec.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/elf/loader/reloc.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/core/resume_queue.cpp
        support/resume_queue_host.cpp
        ${_lifecycle_reloc32_fixture_cpp}
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

add_custom_command(
        OUTPUT ${_fix_dir}/initrd_newc_nosize.cpio.zst
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/compress_initrd.sh zstd-nosize
                ${ASTIA_ZSTD} ${_fix_dir}/initrd_newc.cpio
                ${_fix_dir}/initrd_newc_nosize.cpio.zst
        DEPENDS ${_fix_dir}/initrd_newc.cpio
                ${CMAKE_CURRENT_SOURCE_DIR}/compress_initrd.sh
        COMMENT "Building zstd initrd fixture without a content size"
)
astia_embed_blob(_initrd_zstd_nosize_cpp
        ${_fix_dir}/initrd_newc_nosize.cpio.zst initrd_zstd_nosize_blob)

astia_add_host_test(astia_initrd_test
        initrd_test.cpp
        ${_initrd_newc_cpp}
        ${_initrd_crc_cpp}
        ${_initrd_gzip_cpp}
        ${_initrd_zstd_cpp}
        ${ASTIA_KERNEL_SOURCE_DIR}/src/initrd/initrd.cpp
)

set(_initrd_decoder_third_party ${ASTIA_KERNEL_SOURCE_DIR}/../third_party)
set(_initrd_decoder_sources
        ${ASTIA_KERNEL_SOURCE_DIR}/src/initrd/initrd.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/initrd/decompress.cpp
        ${_initrd_decoder_third_party}/zstd/zstddeclib.c
        ${_initrd_decoder_third_party}/miniz/miniz_tinfl.c
)
astia_add_host_test(astia_initrd_decompress_test
        initrd_decompress_test.cpp
        ${_initrd_decoder_sources}
        ${_initrd_newc_cpp}
        ${_initrd_gzip_cpp}
        ${_initrd_zstd_cpp}
        ${_initrd_zstd_nosize_cpp}
)
target_include_directories(astia_initrd_decompress_test PRIVATE
        ${_initrd_decoder_third_party}/zstd
        ${_initrd_decoder_third_party}/miniz
)
target_compile_definitions(astia_initrd_decompress_test PRIVATE
        MINIZ_NO_MALLOC
        MINIZ_NO_STDIO
        MINIZ_NO_TIME
        MINIZ_NO_ARCHIVE_APIS
        XXH_NO_STDLIB
)
set_source_files_properties(${_initrd_decoder_third_party}/zstd/zstddeclib.c
        ${_initrd_decoder_third_party}/miniz/miniz_tinfl.c
        PROPERTIES
        INCLUDE_DIRECTORIES ${_initrd_decoder_third_party}/compat
        COMPILE_OPTIONS "-fno-sanitize=all"
)

# The initrd VFS allocation-failure path needs the kernel allocator itself, so
# this target deliberately omits the host new shim: `new` returns null exactly
# as it does in the kernel when the heap is exhausted.
add_executable(astia_initrd_fs_test
        initrd_fs_test.cpp
        support/arena.cpp
        support/interrupts_host.cpp
        support/device_ready_host.cpp
        support/pty_vfs_host.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/device.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/blk/blkdev.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/device_file.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/core/resume_queue.cpp
)
set_target_properties(astia_initrd_fs_test PROPERTIES LINK_OPTIONS "")
target_link_libraries(astia_initrd_fs_test PRIVATE c astia_test_support fs)
target_include_directories(astia_initrd_fs_test PRIVATE ${_astia_test_includes}
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev)
target_compile_options(astia_initrd_fs_test PRIVATE
        -fno-rtti -fno-threadsafe-statics -fno-pie)
target_link_options(astia_initrd_fs_test PRIVATE
        "-Wl,-no-pie"
        "-Wl,--defsym=__kernel_start=0x08000000"
        "-Wl,--defsym=__kernel_end=0x08002000")
astia_apply_host_sanitizers(astia_initrd_fs_test)
find_program(ASTIA_INITRD_FS_SETARCH setarch REQUIRED)
add_test(NAME astia_initrd_fs_test
        COMMAND ${ASTIA_INITRD_FS_SETARCH} -R $<TARGET_FILE:astia_initrd_fs_test>)
astia_add_test_label(astia_initrd_fs_test "sanitizer-host-partial;filesystem")

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
         ${_module_lifecycle_cpp}
         ${_module_lifecycle_atexit_cpp}
         ${_module_lifecycle_limit_cpp}
         ${_module_lifecycle_dso_cpp}
         ${_module_lifecycle_async_cpp}
        ${_lifecycle_no_fini_fixture_cpp}
         ${_lifecycle_priority_fixture_cpp}
         ${_lifecycle_wait_fixture_cpp}
         ${_lifecycle_reentrant_fixture_cpp}
         ${_lifecycle_module_api_fixture_cpp}
         ${_lifecycle_reloc32_fixture_cpp}
         ${ASTIA_KERNEL_SOURCE_DIR}/src/mod/module.cpp
         ${ASTIA_KERNEL_SOURCE_DIR}/src/mod/irq_route.cpp
         ${ASTIA_KERNEL_SOURCE_DIR}/src/hw/driver.cpp
         module_cxxabi_host_test.cpp
         ${ASTIA_KERNEL_SOURCE_DIR}/src/core/resume_queue.cpp
         support/resume_queue_host.cpp
         ${ASTIA_KERNEL_SOURCE_DIR}/src/elf/elf.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/elf/loader/exec.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/elf/loader/reloc.cpp
)
target_link_libraries(astia_module_lifecycle_test PRIVATE Threads::Threads)
target_compile_options(astia_module_lifecycle_test PRIVATE
        -ffunction-sections -fdata-sections)
target_compile_definitions(astia_module_lifecycle_test PRIVATE
        ASTIA_MODULE_HOST_TEST=1)
target_link_options(astia_module_lifecycle_test PRIVATE
        "-Wl,-no-pie" "-Wl,--gc-sections")

astia_add_host_test(astia_module_syscall_test
        module_syscall_test.cpp
        module_syscall_host.cpp
        ${_lifecycle_no_fini_fixture_cpp}
        ${_fixture_dyn_cpp}
        ${_fixture_exec_cpp}
        ${ASTIA_KERNEL_SOURCE_DIR}/src/mod/module.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/hw/driver.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/mod/syscalls/modopen.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/mod/syscalls/modls.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/mod/syscalls/modstat.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/mod/syscalls/modclose.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/sys/dispatch.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/sys/syscall.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/core/resume_queue.cpp
        support/resume_queue_host.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/elf/elf.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/elf/loader/exec.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/elf/loader/reloc.cpp
)
target_link_libraries(astia_module_syscall_test PRIVATE Threads::Threads)
target_compile_options(astia_module_syscall_test PRIVATE
        -ffunction-sections -fdata-sections)
target_compile_definitions(astia_module_syscall_test PRIVATE
        ASTIA_MODULE_HOST_TEST=1)
# Discovery walks the astia_syscall_handlers section, which --gc-sections
# drops for executable targets.  Keep the section only for this test target.
target_link_options(astia_module_syscall_test PRIVATE
        "-Wl,-no-pie" "-Wl,--gc-sections")
add_test(NAME astia_kernel_symbol_visibility
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/symbol_table_visibility_test.py
                ${CMAKE_CXX_COMPILER}
                ${ASTIA_KERNEL_SOURCE_DIR}/scripts/generate_module_symbols.py)

astia_add_host_test(astia_ext4_test
        ext4_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/fs/filesystems/ext4/ext4_reader.cpp
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
            ${ASTIA_KERNEL_SOURCE_DIR}/src/fs/filesystems/ext4/ext4_reader.cpp
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
astia_add_host_test(astia_terminal_name_test terminal_name_test.cpp)

astia_add_host_test(astia_memory_test memory_test.cpp ${ASTIA_KERNEL_SOURCE_DIR}/src/lib/memory.cpp)
set_source_files_properties(${ASTIA_KERNEL_SOURCE_DIR}/src/lib/memory.cpp
        PROPERTIES COMPILE_OPTIONS "-fno-builtin")

# Substrate-bound logic: frame_allocator and page_map treat physical addresses
# as pointers. Back a fixed mmap'd window with real memory (support/arena.cpp)
# and inject symbols the allocator reserves. The addresses must match
# astia_test::arena_base / kernel_image_size in support/arena.hpp.
find_program(ASTIA_SETARCH setarch)
if (NOT ASTIA_SETARCH)
    message(FATAL_ERROR
            "setarch is required to run fixed-arena host tests with ASLR disabled")
endif ()
execute_process(
        COMMAND ${ASTIA_SETARCH} -R ${CMAKE_COMMAND} -E true
        RESULT_VARIABLE ASTIA_SETARCH_RESULT
        OUTPUT_QUIET ERROR_QUIET)
if (NOT ASTIA_SETARCH_RESULT EQUAL 0)
    message(FATAL_ERROR
            "setarch -R is unavailable; fixed-arena host tests require ASLR disabled")
endif ()
set(ASTIA_ARENA_TEST_LAUNCHER ${ASTIA_SETARCH} -R)

set(ASTIA_ARENA_ASLR_TEST_NAMES
        astia_exec_rollback_test
        astia_rcu_hashmap_test
        astia_string_test
        astia_frame_allocator_test
        astia_page_map_test
        astia_address_space_test
        astia_heap_test
        astia_phys_region_test
        astia_path_service_test)

function(astia_add_arena_test name)
    if (name IN_LIST ASTIA_ARENA_ASLR_TEST_NAMES)
        set(ASTIA_HOST_TEST_LAUNCHER ${ASTIA_ARENA_TEST_LAUNCHER})
    endif ()
    astia_add_host_test(${name} ${ARGN})
    unset(ASTIA_HOST_TEST_LAUNCHER)
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

# astia::strings::string allocates from mm::heap, so it runs with the heap fixture.
astia_add_arena_test(astia_string_test
        string_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/lib/string.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/mm/heap.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/mm/frame_allocator.cpp
        support/interrupts_host.cpp
)

# Path resolution uses heap-backed scratch so its frame fits the kernel's
# 16 KiB boot/thread stacks even at PATH_MAX=4096.
astia_add_arena_test(astia_path_service_test
        path_service_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/fs/path_service.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/mm/heap.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/mm/frame_allocator.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/mm/placement_new.cpp
        support/arena.cpp
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

# proc TLS arena: the DSO reserve is address space until a probe touches it.
astia_add_arena_test(astia_tls_arena_test
        tls_arena_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc/tls_arena.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/mm/phys_region.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/mm/heap.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/mm/frame_allocator.cpp
        support/interrupts_host.cpp
)

astia_add_arena_test(astia_ext4_vfs_test
        ext4_vfs_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/device.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/blk/blkdev.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/device_file.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/core/resume_queue.cpp
        support/device_ready_host.cpp
        support/pty_vfs_host.cpp
        support/interrupts_host.cpp
)
target_link_libraries(astia_ext4_vfs_test PRIVATE fs)
target_compile_options(astia_ext4_vfs_test PRIVATE -fno-rtti)
target_include_directories(astia_ext4_vfs_test PRIVATE
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev)
astia_remove_test_label(astia_ext4_vfs_test "sanitizer-host")
astia_add_test_label(astia_ext4_vfs_test "sanitizer-host-partial")

astia_add_arena_test(astia_tmpfs_readdir_test
        tmpfs_readdir_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/device.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/blk/blkdev.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/device_file.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/core/resume_queue.cpp
        support/device_ready_host.cpp
        support/pty_vfs_host.cpp
        support/interrupts_host.cpp
)
target_link_libraries(astia_tmpfs_readdir_test PRIVATE fs)
target_compile_options(astia_tmpfs_readdir_test PRIVATE -fno-rtti)
target_include_directories(astia_tmpfs_readdir_test PRIVATE
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev)
astia_remove_test_label(astia_tmpfs_readdir_test "sanitizer-host")
astia_add_test_label(astia_tmpfs_readdir_test "sanitizer-host-partial")

astia_add_arena_test(astia_tmpfs_timestamps_test
        tmpfs_timestamps_test.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/device.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/blk/blkdev.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev/device_file.cpp
        ${ASTIA_KERNEL_SOURCE_DIR}/src/core/resume_queue.cpp
        support/device_ready_host.cpp
        support/pty_vfs_host.cpp
        support/interrupts_host.cpp
)
target_link_libraries(astia_tmpfs_timestamps_test PRIVATE fs)
target_compile_options(astia_tmpfs_timestamps_test PRIVATE -fno-rtti)
target_include_directories(astia_tmpfs_timestamps_test PRIVATE
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev)
astia_remove_test_label(astia_tmpfs_timestamps_test "sanitizer-host")
astia_add_test_label(astia_tmpfs_timestamps_test "sanitizer-host-partial")

# On-target integration test: boot the real kernel under QEMU and let its
# isa-debug-exit status decide pass/fail. Covers what host tests cannot (CR3,
# TLB, real physical memory, the boot path itself).
# Custom-compiled userspace fixtures still need real compile targets so they
# appear in compile_commands.json even when QEMU is unavailable.
if (ARCH STREQUAL "x86_64")
    add_library(astia_module_syscall_init_object OBJECT
            ${CMAKE_CURRENT_SOURCE_DIR}/module_syscall_init.c)
    target_include_directories(astia_module_syscall_init_object PRIVATE
            ${ASTIA_KERNEL_SOURCE_DIR}/include)
    target_compile_options(astia_module_syscall_init_object PRIVATE
            -ffreestanding -fno-pic -fno-pie -fno-builtin
            -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone)
    set_target_properties(astia_module_syscall_init_object PROPERTIES
            C_STANDARD 11 C_STANDARD_REQUIRED ON)

    add_library(astia_module_missing_dependency_object OBJECT
            ${CMAKE_CURRENT_SOURCE_DIR}/module_missing_dependency.cpp)
    target_compile_options(astia_module_missing_dependency_object PRIVATE
            -ffreestanding -fno-pic -fno-pie -mcmodel=large
            -fno-stack-protector -fno-asynchronous-unwind-tables)
    set_target_properties(astia_module_missing_dependency_object PROPERTIES
            CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
endif ()

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
                "core: version=${ASTIA_VERSION_version}"
                "pci: enumerated"
                "selftest: interrupts ok (lapic)"
                "selftest: clock ok"
                "selftest: wait park preemption ok"
                "selftest: uname ok"
                "selftest: statx ok"
        )
        set_tests_properties(astia_kernel_boot PROPERTIES TIMEOUT 300
                ENVIRONMENT "ASTIA_EXPECT_ONCE_PATTERN=core: version=${ASTIA_VERSION_version}")

        add_test(NAME astia_fs_registry_boot
                COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_boot_test.sh
                ${ASTIA_QEMU} $<TARGET_FILE:astiakrn_test>
                ""
                "filesystem registry ok"
        )
        set_tests_properties(astia_fs_registry_boot PROPERTIES TIMEOUT 300)

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
                "selftest: uname ok"
                "selftest: statx ok"
                "smp: discovered=2 online=1"
                "smp: reschedule IPI armed"
        )
        set_tests_properties(astia_kernel_lifecycle_smp2 PROPERTIES
                TIMEOUT 300 ENVIRONMENT "ASTIA_QEMU_SMP=2")

        # Keep a focused SMP boot assertion in addition to the lifecycle run.
        add_test(NAME astia_kernel_smp_boot
                COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_boot_test.sh
                ${ASTIA_QEMU} $<TARGET_FILE:astiakrn_test>
                ""
                "smp: discovered=2 online=1"
                "smp: reschedule IPI armed"
        )
        set_tests_properties(astia_kernel_smp_boot PROPERTIES
                TIMEOUT 300 ENVIRONMENT "ASTIA_QEMU_SMP=2")

        add_test(NAME astia_kernel_scheduler_smp2
                COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_boot_test.sh
                ${ASTIA_QEMU} $<TARGET_FILE:astiakrn_test>
                ""
                "selftest: scheduler ok"
                "smp: discovered=2")
        set_tests_properties(astia_kernel_scheduler_smp2 PROPERTIES
                TIMEOUT 300 ENVIRONMENT "ASTIA_QEMU_SMP=2;ASTIA_QEMU_MACHINE=q35")

        # End-to-end initrd: the same kernel, but the boot module is a cpio image
        # carrying a self-contained ET_REL module. QEMU's PVH path puts -initrd in
        # the modlist, so this covers modlist -> cpio walk -> ELF load -> run.
        add_custom_command(
                OUTPUT ${_fix_dir}/hello_initrd.o
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}
                COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -c -ffreestanding -fno-pic -fno-pie
                -mcmodel=large -fno-stack-protector -fno-asynchronous-unwind-tables
                -fno-exceptions -fno-rtti -fno-use-cxa-atexit -fno-unwind-tables
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                -o ${_fix_dir}/hello_initrd.o ${ASTIA_MODULES_SOURCE_DIR}/tests/hello_initrd.cpp
                DEPENDS ${ASTIA_MODULES_SOURCE_DIR}/tests/hello_initrd.cpp
                ${ASTIA_ET_REL_FIXTURE_HEADERS}
                COMMENT "Compiling ET_REL fixture hello_initrd.o"
        )
        # The libc in ring 3: the same sources the host tests link, but
        # freestanding and non-PIE, with the AIO ports as Astia's side of the
        # seams.  Userspace images link this against tests/crt0.S.
        include(${ASTIA_KERNEL_SOURCE_DIR}/../libc/sources.cmake)
        set(_astia_libc_abs_sources)
        foreach(_astia_libc_source IN LISTS ASTIA_LIBC_ALL_SOURCES)
            list(APPEND _astia_libc_abs_sources
                    ${ASTIA_KERNEL_SOURCE_DIR}/../libc/${_astia_libc_source})
        endforeach()
        add_library(astia_userspace_libc STATIC ${_astia_libc_abs_sources})
        target_include_directories(astia_userspace_libc PRIVATE
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/include
                ${ASTIA_KERNEL_SOURCE_DIR}/include
        )
        target_compile_options(astia_userspace_libc PRIVATE
                -ffreestanding -fno-pic -fno-pie -fno-stack-protector
                -fno-asynchronous-unwind-tables -mno-red-zone -fno-builtin
        )

        # The same sources as a shared library, for the dynamic-linking
        # images.  It cannot depend on a libc, so it is linked freestanding;
        # initial-exec TLS is what the interpreter's static-TLS layout
        # supports.
        get_target_property(_astia_libc_sources astia_userspace_libc SOURCES)
        add_library(astia_userspace_libc_shared SHARED ${_astia_libc_sources})
        target_include_directories(astia_userspace_libc_shared PRIVATE
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/src
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/include
                ${ASTIA_KERNEL_SOURCE_DIR}/include
        )
        target_compile_options(astia_userspace_libc_shared PRIVATE
                -ffreestanding -fPIC -fno-stack-protector
                -fno-asynchronous-unwind-tables -mno-red-zone -fno-builtin
                -ftls-model=initial-exec
        )
        target_link_options(astia_userspace_libc_shared PRIVATE
                -nostdlib -shared -Wl,-soname,libc.so -Wl,-z,now
        )
        target_link_libraries(astia_userspace_libc_shared PRIVATE gcc)
        set_target_properties(astia_userspace_libc_shared PROPERTIES
                PREFIX "" OUTPUT_NAME libc SUFFIX ".so")
        set(_libc_shared_so $<TARGET_FILE:astia_userspace_libc_shared>)

        # A real userspace program: CRT0 unpacks the kernel's startup
        # frame into main(argc, argv, envp) and leaves via libc exit().
        # Prefer the standalone Clang sysroot so this image cannot silently
        # depend on the host compiler's headers or libgcc.
        find_program(ASTIA_CLANG clang)
        add_custom_command(
                OUTPUT ${_fix_dir}/crt0.o
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}
                COMMAND ${CMAKE_C_COMPILER} -c -ffreestanding -fPIE -fno-builtin
                -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone
                -o ${_fix_dir}/crt0.o ${CMAKE_CURRENT_SOURCE_DIR}/crt0.S
                DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/crt0.S
                COMMENT "Compiling userspace CRT0 fixture")
        if (ASTIA_CROSS_USERSPACE AND ASTIA_CLANG)
            set(_cross_dir ${_fix_dir}/clang)
            set(_cross_libc_sources ${_astia_libc_abs_sources})
            file(GLOB _cross_abi_headers CONFIGURE_DEPENDS
                    ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/api/*.h)
            add_custom_command(
                    OUTPUT ${_cross_dir}/image/init_libc
                    BYPRODUCTS ${_cross_dir}/image/libc.so
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
                    ${_cross_abi_headers}
                    COMMENT "Cross-building libc-backed userspace image init_libc"
            )
            set(_init_libc_image ${_cross_dir}/image/init_libc)
            set(_libc_shared_so ${_cross_dir}/image/libc.so)
        else ()
            add_custom_command(
                    OUTPUT ${_fix_dir}/init_libc
                    COMMAND ${CMAKE_C_COMPILER} -c -fPIE -ffreestanding
                    -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone
                    -fno-builtin -I${ASTIA_KERNEL_SOURCE_DIR}/include
                    -I${ASTIA_KERNEL_SOURCE_DIR}/../libc/include
                    -o ${_fix_dir}/init_libc_main.o
                    ${CMAKE_CURRENT_SOURCE_DIR}/init_libc.c
                    COMMAND ${CMAKE_C_COMPILER} -nostdlib -pie -Wl,-e,_start
                    -Wl,--dynamic-linker=/lib/ld.astia.so
                    -Wl,-rpath-link,${_fix_dir} -lgcc
                    -o ${_fix_dir}/init_libc ${_fix_dir}/crt0.o
                    ${_fix_dir}/init_libc_main.o ${_libc_shared_so}
                    DEPENDS ${_fix_dir}/crt0.o
                    ${CMAKE_CURRENT_SOURCE_DIR}/init_libc.c
                    astia_userspace_libc_shared
                    COMMENT "Linking libc-backed userspace image init_libc"
            )
            set(_init_libc_image ${_fix_dir}/init_libc)
        endif ()

    add_custom_command(
            OUTPUT ${_fix_dir}/boot_initrd.cpio
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/boot_stage
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/boot_stage/mod
                COMMAND ${CMAKE_COMMAND} -E copy
                ${_fix_dir}/hello_initrd.o ${_fix_dir}/boot_stage/mod/
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
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -shared -fPIC
                -Wl,-e,_start -o ${_fix_dir}/ld.stub.so
                ${CMAKE_CURRENT_SOURCE_DIR}/interp_stub.S
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -pie -Wl,-e,_start
                -Wl,--dynamic-linker=/lib/ld.stub.so -o ${_fix_dir}/interp_main
                ${CMAKE_CURRENT_SOURCE_DIR}/interp_main.S
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/boot_stage/lib
                COMMAND ${CMAKE_COMMAND} -E copy
                ${_fix_dir}/ld.stub.so ${_fix_dir}/boot_stage/lib/
                COMMAND ${CMAKE_COMMAND} -E copy
                ${_fix_dir}/interp_main ${_fix_dir}/boot_stage/bin/
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -shared -fPIC
                -fno-stack-protector -fno-plt -ffreestanding -mno-red-zone
                -fno-builtin -fno-asynchronous-unwind-tables -Wall -Wextra
                -Wl,-e,ld_start
                -Wl,--hash-style=sysv -Wl,-Bsymbolic -Wl,-z,now
                -I ${ASTIA_KERNEL_SOURCE_DIR}/include
                -I ${ASTIA_KERNEL_SOURCE_DIR}/../libc/include
                -I ${ASTIA_KERNEL_SOURCE_DIR}/../dl/arch/x86_64
                -o ${_fix_dir}/ld.astia.so
                ${ASTIA_KERNEL_SOURCE_DIR}/../dl/arch/x86_64/start.S
                ${ASTIA_KERNEL_SOURCE_DIR}/../dl/arch/x86_64/resolve.S
                ${ASTIA_KERNEL_SOURCE_DIR}/../dl/error.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../dl/ld.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../dl/aio_wait.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../dl/mode.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../dl/symcount.c
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -shared -fPIC
                -ftls-model=initial-exec -fno-stack-protector
                -Wl,--hash-style=sysv -Wl,-z,now
                -Wl,-soname,libfixture_base.so -o ${_fix_dir}/libfixture_base.so
                ${CMAKE_CURRENT_SOURCE_DIR}/fixture_base.c
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -shared -fPIC
                -fno-stack-protector -Wl,--hash-style=sysv -Wl,-z,now
                -Wl,-soname,libcopyfixture.so -o ${_fix_dir}/libcopyfixture.so
                ${CMAKE_CURRENT_SOURCE_DIR}/interp_copy_lib.c
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -no-pie -fno-pic
                -fno-stack-protector -Wl,--hash-style=sysv -Wl,-z,now
                -Wl,-Ttext-segment=0x40000000
                -Wl,-rpath-link,${_fix_dir}
                -Wl,--dynamic-linker=/lib/ld.astia.so
                -I ${ASTIA_KERNEL_SOURCE_DIR}/include
                -o ${_fix_dir}/interp_copy
                ${CMAKE_CURRENT_SOURCE_DIR}/interp_copy.c ${_fix_dir}/libcopyfixture.so
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -shared -fPIC
                -ftls-model=initial-exec -fno-stack-protector
                -Wl,--hash-style=sysv -Wl,-z,now
                -Wl,-soname,libfixture.so -o ${_fix_dir}/libfixture.so
                ${CMAKE_CURRENT_SOURCE_DIR}/fixture_lib.c
                ${_fix_dir}/libfixture_base.so
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -shared -fPIC
                -fno-stack-protector -Wl,--hash-style=sysv -Wl,-z,now
                -Wl,-soname,libfixture_ifunc.so -o ${_fix_dir}/libfixture_ifunc.so
                ${CMAKE_CURRENT_SOURCE_DIR}/fixture_ifunc.c
                ${_fix_dir}/libfixture_base.so
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -shared -fPIC
                -fno-stack-protector -Wl,--hash-style=sysv -Wl,-z,now
                -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/fixture_versions.map
                -Wl,-soname,libfixture_versions.so
                -o ${_fix_dir}/libfixture_versions.so
                ${CMAKE_CURRENT_SOURCE_DIR}/fixture_versions.c
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -pie -fPIC
                -fno-stack-protector -Wl,--hash-style=sysv -Wl,-z,now
                -Wl,-rpath-link,${_fix_dir}
                -Wl,--dynamic-linker=/lib/ld.astia.so -I ${ASTIA_KERNEL_SOURCE_DIR}/include
                -o ${_fix_dir}/interp_ifunc
                ${CMAKE_CURRENT_SOURCE_DIR}/interp_ifunc.c ${_fix_dir}/libfixture_ifunc.so
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -pie -fPIC
                -fno-stack-protector -Wl,--hash-style=sysv -Wl,-z,now
                -Wl,-rpath-link,${_fix_dir}
                -Wl,--dynamic-linker=/lib/ld.astia.so -I ${ASTIA_KERNEL_SOURCE_DIR}/include
                -o ${_fix_dir}/interp_ver
                ${CMAKE_CURRENT_SOURCE_DIR}/interp_ver.c ${_fix_dir}/libfixture_versions.so
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -shared -fPIC
                -fno-stack-protector -Wl,--hash-style=sysv -Wl,-z,now
                -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/fixture_badver.map
                -Wl,-soname,libverstub.so
                -o ${_fix_dir}/libverstub_v1.so
                ${CMAKE_CURRENT_SOURCE_DIR}/fixture_badver.c
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -shared -fPIC -DONLY_V2
                -fno-stack-protector -Wl,--hash-style=sysv -Wl,-z,now
                -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/fixture_badver_v2.map
                -Wl,-soname,libverstub.so
                -o ${_fix_dir}/libverstub.so
                ${CMAKE_CURRENT_SOURCE_DIR}/fixture_badver.c
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -pie -fPIC
                -fno-stack-protector -Wl,--hash-style=sysv -Wl,-z,now
                -Wl,-rpath-link,${_fix_dir}
                -Wl,--dynamic-linker=/lib/ld.astia.so -I ${ASTIA_KERNEL_SOURCE_DIR}/include
                -o ${_fix_dir}/interp_badver
                ${CMAKE_CURRENT_SOURCE_DIR}/interp_badver.c ${_fix_dir}/libverstub_v1.so
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -pie -fPIC
                -fno-stack-protector -Wl,--hash-style=sysv -Wl,-z,lazy
                -Wl,-rpath-link,${_fix_dir}
                -Wl,--dynamic-linker=/lib/ld.astia.so -I ${ASTIA_KERNEL_SOURCE_DIR}/include
                -o ${_fix_dir}/interp_link
                ${CMAKE_CURRENT_SOURCE_DIR}/interp_link.c ${_fix_dir}/libfixture.so
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -shared -fPIC
                -fno-stack-protector -Wl,--hash-style=sysv -Wl,-z,now
                -Wl,-soname,libdlfixture.so -o ${_fix_dir}/libdlfixture.so
                ${CMAKE_CURRENT_SOURCE_DIR}/dl_fixture.c
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -shared -fPIC
                -fno-stack-protector -Wl,--hash-style=sysv -Wl,-z,now
                -Wl,-soname,libdlglobal.so -o ${_fix_dir}/libdlglobal.so
                ${CMAKE_CURRENT_SOURCE_DIR}/dl_fixture_global.c
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -shared -fPIC
                -fno-stack-protector -Wl,--hash-style=sysv -Wl,-z,now
                -Wl,-soname,libdlnd.so -o ${_fix_dir}/libdlnd.so
                ${CMAKE_CURRENT_SOURCE_DIR}/dl_fixture_nd.c
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -shared -fPIC
                -fno-stack-protector -Wl,--hash-style=sysv -Wl,-z,now
                -Wl,-soname,libdlfixture_dep.so -o ${_fix_dir}/libdlfixture_dep.so
                ${CMAKE_CURRENT_SOURCE_DIR}/dl_fixture_dep.c
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -shared -fPIC
                -fno-stack-protector -Wl,--hash-style=sysv -Wl,-z,now
                -Wl,-soname,libdlfixture_user.so -Wl,-rpath-link,${_fix_dir}
                -o ${_fix_dir}/libdlfixture_user.so
                ${CMAKE_CURRENT_SOURCE_DIR}/dl_fixture_user.c
                ${_fix_dir}/libdlfixture_dep.so
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -shared -fPIC
                -fno-stack-protector -Wl,--hash-style=sysv -Wl,-z,now
                -Wl,-soname,libdl_dep_base.so -o ${_fix_dir}/libdl_dep_base.so
                ${CMAKE_CURRENT_SOURCE_DIR}/dl_dep_base.c
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -shared -fPIC
                -fno-stack-protector -Wl,--hash-style=sysv -Wl,-z,now
                -Wl,-soname,libdl_dep_b.so -Wl,-rpath-link,${_fix_dir}
                -o ${_fix_dir}/libdl_dep_b.so
                ${CMAKE_CURRENT_SOURCE_DIR}/dl_dep_b.c
                ${_fix_dir}/libdl_dep_base.so
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -shared -fPIC
                -fno-stack-protector -Wl,--hash-style=sysv -Wl,-z,now
                -Wl,-soname,libdl_dep_c.so -Wl,-rpath-link,${_fix_dir}
                -o ${_fix_dir}/libdl_dep_c.so
                ${CMAKE_CURRENT_SOURCE_DIR}/dl_dep_c.c
                ${_fix_dir}/libdl_dep_base.so
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -shared -fPIC
                -fno-stack-protector -Wl,--hash-style=sysv -Wl,-z,now
                -Wl,-soname,libdl_dep_a.so -Wl,-rpath-link,${_fix_dir}
                -o ${_fix_dir}/libdl_dep_a.so
                ${CMAKE_CURRENT_SOURCE_DIR}/dl_dep_a.c
                ${_fix_dir}/libdl_dep_b.so ${_fix_dir}/libdl_dep_c.so
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -pie -fPIC
                -fno-stack-protector -Wl,--hash-style=sysv -Wl,-z,now
                -Wl,--dynamic-linker=/lib/ld.astia.so
                -I ${ASTIA_KERNEL_SOURCE_DIR}/include
                -I ${ASTIA_KERNEL_SOURCE_DIR}/../libc/include
                -o ${_fix_dir}/interp_dlopen
                ${CMAKE_CURRENT_SOURCE_DIR}/interp_dlopen.c
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -pie -fPIC
                -fno-stack-protector -Wl,--hash-style=sysv -Wl,-z,now -DEXPECT_EAGER
                -Wl,-rpath-link,${_fix_dir}
                -Wl,--dynamic-linker=/lib/ld.astia.so -I ${ASTIA_KERNEL_SOURCE_DIR}/include
                -o ${_fix_dir}/interp_now
                ${CMAKE_CURRENT_SOURCE_DIR}/interp_link.c ${_fix_dir}/libfixture.so
                COMMAND ${CMAKE_COMMAND} -E copy
                ${_fix_dir}/ld.astia.so ${_fix_dir}/boot_stage/lib/
                COMMAND ${CMAKE_COMMAND} -E copy
                ${_fix_dir}/libfixture.so ${_fix_dir}/libfixture_base.so
                ${_fix_dir}/libfixture_ifunc.so ${_fix_dir}/libcopyfixture.so
                ${_fix_dir}/libdlfixture.so ${_fix_dir}/libdlglobal.so
                ${_fix_dir}/libdlnd.so ${_fix_dir}/libdlfixture_dep.so
                ${_fix_dir}/libdlfixture_user.so
                ${_fix_dir}/libdl_dep_base.so ${_fix_dir}/libdl_dep_b.so
                ${_fix_dir}/libdl_dep_c.so ${_fix_dir}/libdl_dep_a.so
                ${_fix_dir}/boot_stage/lib/
                COMMAND ${CMAKE_COMMAND} -E copy
                ${_fix_dir}/interp_link ${_fix_dir}/interp_ifunc
                ${_fix_dir}/interp_copy ${_fix_dir}/interp_dlopen ${_fix_dir}/interp_now
                ${_fix_dir}/boot_stage/bin/
                COMMAND ${CMAKE_COMMAND} -E copy
                ${_fix_dir}/libfixture_versions.so ${_fix_dir}/libverstub.so
                ${_fix_dir}/boot_stage/lib/
                COMMAND ${CMAKE_COMMAND} -E copy
                ${_fix_dir}/interp_ver ${_fix_dir}/interp_badver
                ${_fix_dir}/boot_stage/bin/
                COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/strip_pt_phdr.py
                ${_fix_dir}/interp_link ${_fix_dir}/interp_nophdr
                COMMAND ${CMAKE_COMMAND} -E copy
                ${_fix_dir}/interp_nophdr ${_fix_dir}/boot_stage/bin/
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -shared -fPIC
                -fno-stack-protector -Wl,--hash-style=gnu
                -Wl,-soname,libfixture_gnu.so -o ${_fix_dir}/libfixture_gnu.so
                ${CMAKE_CURRENT_SOURCE_DIR}/fixture_gnu.c
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -shared -fPIC
                -fno-stack-protector -Wl,--hash-style=both
                -Wl,-soname,libfixture_semihash.so
                -o ${_fix_dir}/libfixture_semihash.so
                ${CMAKE_CURRENT_SOURCE_DIR}/fixture_semihash.c
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -pie -fPIC
                -fno-stack-protector -Wl,--hash-style=gnu
                -Wl,-rpath-link,${_fix_dir}
                -Wl,--dynamic-linker=/lib/ld.astia.so
                -I ${ASTIA_KERNEL_SOURCE_DIR}/include
                -o ${_fix_dir}/interp_gnu
                ${CMAKE_CURRENT_SOURCE_DIR}/interp_gnu.c
                ${_fix_dir}/libfixture_gnu.so ${_fix_dir}/libfixture_semihash.so
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -shared -fPIC
                -Wl,--hash-style=sysv -Wl,-soname,libfixture_tpoff32.so
                -o ${_fix_dir}/libfixture_tpoff32.link.so
                ${CMAKE_CURRENT_SOURCE_DIR}/fixture_tpoff32.S
                ${_fix_dir}/libfixture_base.so
                COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/reloc_type_rewrite.py 18 23
                ${_fix_dir}/libfixture_tpoff32.link.so
                ${_fix_dir}/libfixture_tpoff32.so
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -pie -fPIC
                -fno-stack-protector -Wl,--hash-style=sysv
                -Wl,-rpath-link,${_fix_dir}
                -Wl,--dynamic-linker=/lib/ld.astia.so
                -I ${ASTIA_KERNEL_SOURCE_DIR}/include
                -o ${_fix_dir}/interp_tpoff32
                ${CMAKE_CURRENT_SOURCE_DIR}/interp_tpoff32.c
                ${_fix_dir}/libfixture_tpoff32.so
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -pie -fPIC
                -fno-stack-protector -Wl,--hash-style=sysv
                -Wl,--dynamic-linker=/lib/ld.astia.so
                -I ${ASTIA_KERNEL_SOURCE_DIR}/include
                -o ${_fix_dir}/interp_relative_ro
                ${CMAKE_CURRENT_SOURCE_DIR}/interp_relative_ro.c
                ${CMAKE_CURRENT_SOURCE_DIR}/interp_relative_ro.S
                COMMAND ${CMAKE_COMMAND} -E copy
                ${_fix_dir}/libfixture_gnu.so ${_fix_dir}/libfixture_semihash.so
                ${_fix_dir}/libfixture_tpoff32.so ${_fix_dir}/boot_stage/lib/
                COMMAND ${CMAKE_COMMAND} -E copy
                ${_fix_dir}/interp_gnu ${_fix_dir}/interp_tpoff32
                ${_fix_dir}/interp_relative_ro ${_fix_dir}/boot_stage/bin/
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -pie -fPIC
                -Wl,-z,relro -Wl,-z,now -Wl,--hash-style=sysv
                -Wl,--dynamic-linker=/lib/ld.astia.so
                -I ${ASTIA_KERNEL_SOURCE_DIR}/include
                -o ${_fix_dir}/interp_relro
                ${CMAKE_CURRENT_SOURCE_DIR}/interp_relro.c
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -pie -fPIC
                -DRELRO_CONTROL
                -Wl,-z,relro -Wl,-z,now -Wl,--hash-style=sysv
                -Wl,--dynamic-linker=/lib/ld.astia.so
                -I ${ASTIA_KERNEL_SOURCE_DIR}/include
                -o ${_fix_dir}/interp_relro_ok
                ${CMAKE_CURRENT_SOURCE_DIR}/interp_relro.c
                COMMAND ${CMAKE_COMMAND} -E copy
                ${_fix_dir}/interp_relro ${_fix_dir}/interp_relro_ok
                ${_fix_dir}/boot_stage/bin/
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -shared -fPIC
                -fno-stack-protector -Wl,--hash-style=sysv -Wl,-z,now
                -Wl,-soname,libfixture_dynamic_tls.so
                -o ${_fix_dir}/libfixture_dynamic_tls.so
                ${CMAKE_CURRENT_SOURCE_DIR}/fixture_dynamic_tls.c
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -pie -fPIC
                -fno-stack-protector -Wl,--hash-style=sysv -Wl,-z,now
                -Wl,-rpath-link,${_fix_dir} -Wl,--allow-shlib-undefined
                -Wl,--dynamic-linker=/lib/ld.astia.so
                -I ${ASTIA_KERNEL_SOURCE_DIR}/include
                -o ${_fix_dir}/interp_dyntls
                ${CMAKE_CURRENT_SOURCE_DIR}/interp_dyntls.c
                ${_fix_dir}/libfixture_dynamic_tls.so
                COMMAND ${CMAKE_COMMAND} -E copy
                ${_fix_dir}/libfixture_dynamic_tls.so ${_fix_dir}/boot_stage/lib/
                COMMAND ${CMAKE_COMMAND} -E copy
                ${_fix_dir}/interp_dyntls ${_fix_dir}/boot_stage/bin/
                COMMAND ${CMAKE_C_COMPILER} -c -fPIE -ffreestanding
                -fno-stack-protector -mno-red-zone
                -I ${ASTIA_KERNEL_SOURCE_DIR}/../libc/include
                -I ${ASTIA_KERNEL_SOURCE_DIR}/include
                -o ${_fix_dir}/dyn_libc_main.o ${CMAKE_CURRENT_SOURCE_DIR}/dyn_libc.c
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -pie -Wl,-e,_start
                -Wl,--dynamic-linker=/lib/ld.astia.so
                -Wl,-rpath-link,${_fix_dir}
                -o ${_fix_dir}/dyn_libc ${_fix_dir}/crt0.o
                ${_fix_dir}/dyn_libc_main.o ${_libc_shared_so}
                -lgcc
                COMMAND ${CMAKE_COMMAND} -E copy
                ${_libc_shared_so} ${_fix_dir}/boot_stage/lib/
                COMMAND ${CMAKE_COMMAND} -E copy
                ${_fix_dir}/dyn_libc ${_fix_dir}/boot_stage/bin/
                COMMAND ${CMAKE_COMMAND} -E make_directory
                ${_fix_dir}/boot_stage/jail/lib
                COMMAND ${CMAKE_COMMAND} -E make_directory
                ${_fix_dir}/boot_stage/jail/etc
                COMMAND ${CMAKE_COMMAND} -E copy
                ${_libc_shared_so} ${_fix_dir}/boot_stage/jail/lib/libc.so
                COMMAND ${CMAKE_COMMAND} -E copy
                ${_fix_dir}/ld.astia.so ${_fix_dir}/boot_stage/jail/lib/
                COMMAND ${CMAKE_COMMAND} -E copy
                ${_init_libc_image} ${_fix_dir}/boot_stage/jail/init_libc
                COMMAND ${CMAKE_COMMAND} -E copy
                ${ASTIA_KERNEL_SOURCE_DIR}/../daemond/passwd ${_fix_dir}/boot_stage/jail/etc/passwd
                COMMAND ${CMAKE_COMMAND} -E env ASTIA_CPIO=${ASTIA_CPIO}
                sh ${CMAKE_CURRENT_SOURCE_DIR}/make_cpio_archive.sh
                ${_fix_dir}/boot_stage ${_fix_dir}/boot_initrd.cpio
                mod sbin/init sbin/truncated-elf bin/init_libc
                bin/interp_main lib/ld.stub.so lib/ld.astia.so lib/libfixture.so
                lib/libfixture_base.so lib/libfixture_ifunc.so
                bin/interp_link bin/interp_now bin/interp_nophdr bin/interp_ifunc
                bin/interp_copy lib/libcopyfixture.so
                bin/interp_relro bin/interp_relro_ok
                lib/libdlfixture.so lib/libdlglobal.so lib/libdlnd.so
                lib/libdlfixture_dep.so lib/libdlfixture_user.so
                lib/libdl_dep_base.so lib/libdl_dep_b.so lib/libdl_dep_c.so
                lib/libdl_dep_a.so
                bin/interp_dlopen
                lib/libfixture_dynamic_tls.so bin/interp_dyntls
                lib/libfixture_versions.so lib/libverstub.so bin/interp_ver
                bin/interp_badver
                lib/libc.so bin/dyn_libc etc/passwd etc/group
                lib/libfixture_gnu.so lib/libfixture_semihash.so
                lib/libfixture_tpoff32.so bin/interp_gnu bin/interp_tpoff32
                bin/interp_relative_ro
                jail/init_libc jail/lib/libc.so jail/lib/ld.astia.so jail/etc/passwd
                DEPENDS ${_fix_dir}/hello_initrd.o ${_fix_dir}/fixture_exec.elf
                ${CMAKE_CURRENT_SOURCE_DIR}/make_truncated_elf.sh
                ${CMAKE_CURRENT_SOURCE_DIR}/init_user.c ${_init_libc_image}
                ${CMAKE_CURRENT_SOURCE_DIR}/interp_stub.S
                ${CMAKE_CURRENT_SOURCE_DIR}/interp_main.S
                ${ASTIA_KERNEL_SOURCE_DIR}/../dl/error.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../dl/error.h
                ${ASTIA_KERNEL_SOURCE_DIR}/../dl/ld.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../dl/ld.h
                ${ASTIA_KERNEL_SOURCE_DIR}/../dl/aio_wait.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../dl/aio_wait.h
                ${ASTIA_KERNEL_SOURCE_DIR}/../dl/mode.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../dl/mode.h
                ${ASTIA_KERNEL_SOURCE_DIR}/../dl/symcount.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../dl/symcount.h
                ${ASTIA_KERNEL_SOURCE_DIR}/../libc/include/dlfcn.h
                ${ASTIA_KERNEL_SOURCE_DIR}/../dl/arch/x86_64/start.S
                ${ASTIA_KERNEL_SOURCE_DIR}/../dl/arch/x86_64/resolve.S
                ${ASTIA_KERNEL_SOURCE_DIR}/../dl/arch/x86_64/reloc.h
                ${CMAKE_CURRENT_SOURCE_DIR}/fixture_lib.c
                ${CMAKE_CURRENT_SOURCE_DIR}/fixture_base.c
                ${CMAKE_CURRENT_SOURCE_DIR}/fixture_gnu.c
                ${CMAKE_CURRENT_SOURCE_DIR}/fixture_semihash.c
                ${CMAKE_CURRENT_SOURCE_DIR}/fixture_tpoff32.S
                ${CMAKE_CURRENT_SOURCE_DIR}/fixture_ifunc.c
                ${CMAKE_CURRENT_SOURCE_DIR}/interp_link.c
                ${CMAKE_CURRENT_SOURCE_DIR}/interp_copy.c
                ${CMAKE_CURRENT_SOURCE_DIR}/interp_copy_lib.c
                ${CMAKE_CURRENT_SOURCE_DIR}/interp_gnu.c
                ${CMAKE_CURRENT_SOURCE_DIR}/interp_tpoff32.c
                ${CMAKE_CURRENT_SOURCE_DIR}/interp_relative_ro.c
                ${CMAKE_CURRENT_SOURCE_DIR}/interp_relative_ro.S
                ${CMAKE_CURRENT_SOURCE_DIR}/interp_ifunc.c
                ${CMAKE_CURRENT_SOURCE_DIR}/reloc_type_rewrite.py
                ${CMAKE_CURRENT_SOURCE_DIR}/interp_relro.c
                ${CMAKE_CURRENT_SOURCE_DIR}/interp_dlopen.c
                ${CMAKE_CURRENT_SOURCE_DIR}/fixture_dynamic_tls.c
                ${CMAKE_CURRENT_SOURCE_DIR}/interp_dyntls.c
                ${CMAKE_CURRENT_SOURCE_DIR}/fixture_versions.c
                ${CMAKE_CURRENT_SOURCE_DIR}/fixture_versions.map
                ${CMAKE_CURRENT_SOURCE_DIR}/interp_ver.c
                ${CMAKE_CURRENT_SOURCE_DIR}/fixture_badver.c
                ${CMAKE_CURRENT_SOURCE_DIR}/fixture_badver.map
                ${CMAKE_CURRENT_SOURCE_DIR}/fixture_badver_v2.map
                ${CMAKE_CURRENT_SOURCE_DIR}/interp_badver.c
                ${CMAKE_CURRENT_SOURCE_DIR}/dyn_libc.c
                ${_fix_dir}/crt0.o
                ${_libc_shared_so}
                ${ASTIA_KERNEL_SOURCE_DIR}/../daemond/passwd ${ASTIA_KERNEL_SOURCE_DIR}/../daemond/group
                ${CMAKE_CURRENT_SOURCE_DIR}/make_cpio_archive.sh
                COMMENT "Packing boot initrd boot_initrd.cpio"
        )
        # Cpio images are test inputs, not sources of any target, so build them as
        # part of the default build to guarantee they exist before ctest runs.
        add_custom_target(astia_boot_initrd ALL DEPENDS ${_fix_dir}/boot_initrd.cpio)

        add_custom_command(
                OUTPUT ${_fix_dir}/boot_initrd.cpio.gz
                COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/compress_initrd.sh gzip
                ${ASTIA_GZIP} ${_fix_dir}/boot_initrd.cpio
                ${_fix_dir}/boot_initrd.cpio.gz
                DEPENDS ${_fix_dir}/boot_initrd.cpio
                ${CMAKE_CURRENT_SOURCE_DIR}/compress_initrd.sh
                COMMENT "Compressing boot initrd with gzip")
        add_custom_target(astia_boot_initrd_gzip ALL
                DEPENDS ${_fix_dir}/boot_initrd.cpio.gz)

        add_custom_command(
                OUTPUT ${_fix_dir}/boot_initrd.cpio.zst
                COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/compress_initrd.sh zstd
                ${ASTIA_ZSTD} ${_fix_dir}/boot_initrd.cpio
                ${_fix_dir}/boot_initrd.cpio.zst
                DEPENDS ${_fix_dir}/boot_initrd.cpio
                ${CMAKE_CURRENT_SOURCE_DIR}/compress_initrd.sh
                COMMENT "Compressing boot initrd with zstd")
        add_custom_target(astia_boot_initrd_zstd ALL
                DEPENDS ${_fix_dir}/boot_initrd.cpio.zst)

        add_custom_command(
                OUTPUT ${_fix_dir}/module_missing_dependency.o
                COMMAND ${CMAKE_COMMAND} -E copy
                $<TARGET_OBJECTS:astia_module_missing_dependency_object>
                ${_fix_dir}/module_missing_dependency.o
                DEPENDS astia_module_missing_dependency_object
                COMMENT "Copying invalid-dependency ET_REL fixture")

        add_custom_command(
                OUTPUT ${_fix_dir}/module_syscall_init
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -static -Wl,-no-pie
                -Wl,-e,_start -Wl,-Ttext-segment=0x40000000
                -o ${_fix_dir}/module_syscall_init
                $<TARGET_OBJECTS:astia_module_syscall_init_object>
                DEPENDS astia_module_syscall_init_object
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/api/syscalls.h
                COMMENT "Linking module syscall userspace fixture")

        add_custom_command(
                OUTPUT ${_fix_dir}/module_syscall_initrd.cpio
                COMMAND ${CMAKE_COMMAND} -E make_directory
                ${_fix_dir}/module_syscall_stage/sbin
                ${_fix_dir}/module_syscall_stage/bin
                ${_fix_dir}/module_syscall_stage/mod
                ${_fix_dir}/module_syscall_stage/etc
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/module_syscall_init
                ${_fix_dir}/module_syscall_stage/sbin/init
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/module_syscall_init
                ${_fix_dir}/module_syscall_stage/bin/init_libc
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/hello_initrd.o
                ${_fix_dir}/module_syscall_stage/mod/hello.o
                COMMAND ${CMAKE_COMMAND} -E copy
                ${_fix_dir}/module_missing_dependency.o
                ${_fix_dir}/module_syscall_stage/mod/bad-dependency.o
                COMMAND dd if=${_fix_dir}/hello_initrd.o
                of=${_fix_dir}/module_syscall_stage/mod/truncated.elf
                bs=64 count=1 status=none
                COMMAND dd if=/dev/zero
                of=${_fix_dir}/module_syscall_stage/mod/huge.bin
                bs=1024 count=1025 status=none
                COMMAND ${CMAKE_COMMAND} -E copy
                ${ASTIA_KERNEL_SOURCE_DIR}/../daemond/passwd
                ${_fix_dir}/module_syscall_stage/etc/passwd
                COMMAND ${CMAKE_COMMAND} -E env ASTIA_CPIO=${ASTIA_CPIO}
                sh ${CMAKE_CURRENT_SOURCE_DIR}/make_cpio_archive.sh
                ${_fix_dir}/module_syscall_stage
                ${_fix_dir}/module_syscall_initrd.cpio
                mod sbin/init bin/init_libc etc/passwd
                DEPENDS ${_fix_dir}/module_syscall_init
                ${_fix_dir}/hello_initrd.o ${_fix_dir}/module_missing_dependency.o
                ${ASTIA_KERNEL_SOURCE_DIR}/../daemond/passwd
                ${CMAKE_CURRENT_SOURCE_DIR}/make_cpio_archive.sh
                COMMENT "Packing module syscall initrd")
        add_custom_target(astia_module_syscall_initrd ALL
                DEPENDS ${_fix_dir}/module_syscall_initrd.cpio)

        add_test(NAME astia_module_syscall_boot
                COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_boot_test.sh
                ${ASTIA_QEMU} $<TARGET_FILE:astiakrn_test>
                ${_fix_dir}/module_syscall_initrd.cpio
                "hello.o rc=0"
                "spawn reaped pid=1 status=0 path=/sbin/init"
                "path=/bin/init_libc"
                "core: selftest ok"
        )
        set_tests_properties(astia_module_syscall_boot PROPERTIES TIMEOUT 300)

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
                "mod: mod/hello_initrd.o rc=0"
                "selftest: statx ok"
                "user thread exit pid="
                "spawn reaped pid=1 status=0 path=/sbin/init"
                "astia: libc ok"
                "astia: exit parked waiters start"
                "astia: exec permission ok"
                "astia: random ok"
                "astia: interp ok"
                "astia: w^x ok"
                "astia: copy reloc ok"
                "astia: relro ok"
                "astia: ver ok"
                "astia: ver reject ok"
                "astia: ld ok"
                "astia: dyntls ok"
                "astia: ld eager ok"
                "astia: ld nophdr ok"
                "astia: gnu-hash ok"
                "astia: tpoff32 ok"
                "astia: relative ro ok"
                "astia: exec repeat ok"
                "astia: dyn tls clone ok"
                "astia: dl binding enospc ok"
                "astia: ifunc ok"
                "astia: sigpipe ok"
                "astia: setpgid exec ok"
                "astia: libc.so ok"
                "astia: dlopen ok"
        )
        set_tests_properties(astia_initrd_boot PROPERTIES TIMEOUT 300)

        add_test(NAME astia_chroot
                COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_boot_test.sh
                ${ASTIA_QEMU} $<TARGET_FILE:astiakrn_test>
                ${_fix_dir}/boot_initrd.cpio
                "astia: chroot ok"
                "astia: chroot exec ok"
                "astia: libc ok"
        )
        set_tests_properties(astia_chroot PROPERTIES TIMEOUT 300)

        add_test(NAME astia_urandom_fresh_boot
                COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_urandom_boot_test.sh
                ${ASTIA_QEMU} $<TARGET_FILE:astiakrn_test>
                ${_fix_dir}/boot_initrd.cpio)
        set_tests_properties(astia_urandom_fresh_boot PROPERTIES TIMEOUT 360)

        add_test(NAME astia_initrd_boot_gzip
                COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_boot_test.sh
                ${ASTIA_QEMU} $<TARGET_FILE:astiakrn_test>
                ${_fix_dir}/boot_initrd.cpio.gz
                "fs: initrd: gzip container"
                "mod: mod/hello_initrd.o rc=0"
                "selftest: statx ok"
                "astia: libc ok"
                "astia: ld ok"
                "astia: gnu-hash ok"
                "astia: tpoff32 ok"
                "astia: relative ro ok"
                "astia: exec repeat ok"
                "astia: dyn tls clone ok"
                "astia: dl binding enospc ok"
        )
        set_tests_properties(astia_initrd_boot_gzip PROPERTIES TIMEOUT 300)

        add_test(NAME astia_initrd_boot_zstd
                COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_boot_test.sh
                ${ASTIA_QEMU} $<TARGET_FILE:astiakrn_test>
                ${_fix_dir}/boot_initrd.cpio.zst
                "fs: initrd: zstd container"
                "mod: mod/hello_initrd.o rc=0"
                "selftest: statx ok"
                "astia: libc ok"
                "astia: ld ok"
                "astia: gnu-hash ok"
                "astia: tpoff32 ok"
                "astia: relative ro ok"
                "astia: exec repeat ok"
                "astia: dyn tls clone ok"
                "astia: dl binding enospc ok"
        )
        set_tests_properties(astia_initrd_boot_zstd PROPERTIES TIMEOUT 300)

        add_test(NAME astia_production_init_exit
                COMMAND ${CMAKE_COMMAND} -E env
                QEMU=${ASTIA_QEMU}
                ASTIA_KERNEL=$<TARGET_FILE:astiakrn>
                ASTIA_INITRD=${_fix_dir}/production_exit_initrd.cpio
                PYTHON=${Python3_EXECUTABLE}
                sh ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_init_exit.sh)
        set_tests_properties(astia_production_init_exit PROPERTIES TIMEOUT 30
                FIXTURES_REQUIRED astia-qemu-artifacts)

        # Native PID-1 split: init only bootstraps/waits for daemond; daemond owns
        # the fixed-capacity, non-shell rc file.  Keep this as a separate image
        # from init_user, whose purpose is the raw syscall ABI regression.
        function(astia_add_native_program name source extra_include)
            set(_native_object ${_fix_dir}/${name}.o)
            set(_native_objects ${_native_object})
            set(_native_extra_objects)
            set(_native_sources ${source})
            if (extra_include)
                set(_native_extra_include -I${extra_include})
            else ()
                set(_native_extra_include)
            endif ()
            foreach (_extra_source IN LISTS ARGN)
                get_filename_component(_extra_name ${_extra_source} NAME_WE)
                set(_extra_object ${_fix_dir}/${name}-${_extra_name}.o)
                add_custom_command(
                        OUTPUT ${_extra_object}
                        COMMAND ${CMAKE_C_COMPILER} -c -fPIE -ffreestanding
                        -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone
                        -fno-builtin -I${ASTIA_KERNEL_SOURCE_DIR}/../libc/include
                        -I${ASTIA_KERNEL_SOURCE_DIR}/include ${_native_extra_include}
                        -o ${_extra_object} ${_extra_source}
                        DEPENDS ${_extra_source}
                        COMMENT "Building native ${name} ${_extra_name} object")
                list(APPEND _native_objects ${_extra_object})
                list(APPEND _native_extra_objects ${_extra_object})
                list(APPEND _native_sources ${_extra_source})
            endforeach ()
            add_custom_command(
                    OUTPUT ${_fix_dir}/${name}
                    COMMAND ${CMAKE_C_COMPILER} -c -fPIE -ffreestanding
                    -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone
                    -fno-builtin -I${ASTIA_KERNEL_SOURCE_DIR}/../libc/include
                    -I${ASTIA_KERNEL_SOURCE_DIR}/include ${_native_extra_include}
                    -o ${_native_object} ${source}
                    COMMAND ${CMAKE_C_COMPILER} -nostdlib -pie -Wl,-e,_start
                    -Wl,--dynamic-linker=/lib/ld.astia.so
                    -Wl,-rpath-link,${_fix_dir}
                    -o ${_fix_dir}/${name} ${_fix_dir}/crt0.o ${_native_objects}
                    ${_libc_shared_so} -lgcc
                    DEPENDS ${_native_sources} ${_native_extra_objects}
                    ${ASTIA_KERNEL_SOURCE_DIR}/../daemond/daemond.h
                    ${ASTIA_KERNEL_SOURCE_DIR}/../libc/include/daemon_protocol.h
                    ${ASTIA_KERNEL_SOURCE_DIR}/../coreutils/src/common.h
                    ${_fix_dir}/crt0.o astia_userspace_libc_shared
                    ${_libc_shared_so}
                    COMMENT "Building native ${name} image")
        endfunction()
        astia_add_native_program(native_init
                ${ASTIA_KERNEL_SOURCE_DIR}/../init/init.c "")
        astia_add_native_program(native_daemond
                ${ASTIA_KERNEL_SOURCE_DIR}/../daemond/daemond.c ""
                ${ASTIA_KERNEL_SOURCE_DIR}/../daemond/stop.c)
        astia_add_native_program(native_daemondctl
                ${ASTIA_KERNEL_SOURCE_DIR}/../coreutils/src/daemonctl.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../coreutils/src)
        astia_add_native_program(native_ps
                ${ASTIA_KERNEL_SOURCE_DIR}/../coreutils/src/ps.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../coreutils/src)
        astia_add_native_program(native_true
                ${ASTIA_KERNEL_SOURCE_DIR}/../coreutils/src/true.c
                ${ASTIA_KERNEL_SOURCE_DIR}/../coreutils/src)
        astia_add_native_program(native_stop_client
                ${ASTIA_KERNEL_SOURCE_DIR}/../daemond/tests/stop_client.c "")
        add_custom_target(astia_native_programs
                DEPENDS ${_fix_dir}/native_init ${_fix_dir}/native_daemond
                ${_fix_dir}/native_daemondctl ${_fix_dir}/native_ps ${_fix_dir}/native_true
                ${_fix_dir}/native_stop_client)
        add_custom_command(
                OUTPUT ${_fix_dir}/native_boot_initrd.cpio
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/native_stage/sbin
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/native_stage/bin
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/native_stage/etc
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/native_stage/lib
                COMMAND ${CMAKE_COMMAND} -E copy ${_libc_shared_so}
                ${_fix_dir}/native_stage/lib/libc.so
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/ld.astia.so
                ${_fix_dir}/native_stage/lib/ld.astia.so
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
                lib/libc.so lib/ld.astia.so
                DEPENDS ${_fix_dir}/native_init ${_fix_dir}/native_daemond
                ${_fix_dir}/native_daemondctl ${_fix_dir}/native_ps
                ${_init_libc_image} ${ASTIA_KERNEL_SOURCE_DIR}/../daemond/rc
                ${_fix_dir}/boot_initrd.cpio ${_libc_shared_so}
                astia_userspace_libc_shared
                ${CMAKE_CURRENT_SOURCE_DIR}/make_cpio_archive.sh
                COMMENT "Packing native init/daemond initrd")
        # The stock rc has no service records.  daemond must stay resident
        # instead of exiting and taking PID 1 down, so the kernel never
        # finishes its lifecycle; the bounded timeout is the expected
        # outcome and the patterns pin the reason.
        add_test(NAME astia_native_daemond_idle
                COMMAND env
                ASTIA_EXPECT_TIMEOUT=1
                ASTIA_REQUIRE_SELFTEST=0
                ASTIA_BOOT_QEMU_TIMEOUT=6
                "ASTIA_FORBID_PATTERN=daemond: clean shutdown"
                sh ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_boot_test.sh
                ${ASTIA_QEMU} $<TARGET_FILE:astiakrn_test>
                ${_fix_dir}/native_boot_initrd.cpio
                "init: daemond started"
                "daemond: started"
                "daemond: no services configured; idling")
        set_tests_properties(astia_native_daemond_idle PROPERTIES TIMEOUT 60)

        add_custom_command(
                OUTPUT ${_fix_dir}/native_service_boot_initrd.cpio
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/native_service_stage/sbin
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/native_service_stage/bin
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/native_service_stage/etc
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/native_service_stage/lib
                COMMAND ${CMAKE_COMMAND} -E copy ${_libc_shared_so}
                ${_fix_dir}/native_service_stage/lib/libc.so
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/ld.astia.so
                ${_fix_dir}/native_service_stage/lib/ld.astia.so
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/native_init ${_fix_dir}/native_service_stage/sbin/init
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/native_daemond ${_fix_dir}/native_service_stage/sbin/daemond
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/native_true ${_fix_dir}/native_service_stage/bin/true
                COMMAND ${CMAKE_COMMAND} -E copy ${_init_libc_image} ${_fix_dir}/native_service_stage/bin/init_libc
                COMMAND ${CMAKE_COMMAND} -E copy ${ASTIA_KERNEL_SOURCE_DIR}/../daemond/service.rc ${_fix_dir}/native_service_stage/etc/rc
                COMMAND ${CMAKE_COMMAND} -E env ASTIA_CPIO=${ASTIA_CPIO}
                sh ${CMAKE_CURRENT_SOURCE_DIR}/make_cpio_archive.sh
                ${_fix_dir}/native_service_stage ${_fix_dir}/native_service_boot_initrd.cpio
                sbin/init sbin/daemond bin/true bin/init_libc etc/rc
                lib/libc.so lib/ld.astia.so
                DEPENDS ${_fix_dir}/native_init ${_fix_dir}/native_daemond
                ${_fix_dir}/native_true ${_init_libc_image}
                ${_fix_dir}/boot_initrd.cpio ${_libc_shared_so}
                astia_userspace_libc_shared
                ${ASTIA_KERNEL_SOURCE_DIR}/../daemond/service.rc
                ${CMAKE_CURRENT_SOURCE_DIR}/make_cpio_archive.sh
                COMMENT "Packing native service supervision initrd")
        add_test(NAME astia_native_daemond_service
                COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_boot_test.sh
                ${ASTIA_QEMU} $<TARGET_FILE:astiakrn_test>
                ${_fix_dir}/native_service_boot_initrd.cpio
                "init: daemond started"
                "daemond: reaped service pid="
                "daemond: restarting service: /bin/true attempt=1"
                "daemond: clean shutdown"
                "init: clean shutdown")
        set_tests_properties(astia_native_daemond_service PROPERTIES TIMEOUT 300)
        add_custom_target(astia_native_service_boot_initrd ALL
                DEPENDS ${_fix_dir}/native_service_boot_initrd.cpio)

        # SIGTERM escalation: the supervised service ignores SIGTERM, while a
        # child outside its process group asks daemonctl to stop.  The
        # supervisor must SIGKILL the service before it reports success, then
        # shut down cleanly (a failed stop keeps it resident and times out).
        add_custom_command(
                OUTPUT ${_fix_dir}/native_escalation_boot_initrd.cpio
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/native_escalation_stage/sbin
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/native_escalation_stage/bin
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/native_escalation_stage/etc
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/native_escalation_stage/lib
                COMMAND ${CMAKE_COMMAND} -E copy ${_libc_shared_so}
                ${_fix_dir}/native_escalation_stage/lib/libc.so
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/ld.astia.so
                ${_fix_dir}/native_escalation_stage/lib/ld.astia.so
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/native_init ${_fix_dir}/native_escalation_stage/sbin/init
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/native_daemond ${_fix_dir}/native_escalation_stage/sbin/daemond
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/native_daemondctl ${_fix_dir}/native_escalation_stage/bin/daemonctl
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/native_stop_client ${_fix_dir}/native_escalation_stage/bin/stop-client
                COMMAND ${CMAKE_COMMAND} -E copy ${_init_libc_image} ${_fix_dir}/native_escalation_stage/bin/init_libc
                COMMAND ${CMAKE_COMMAND} -E copy ${ASTIA_KERNEL_SOURCE_DIR}/../daemond/escalation.rc ${_fix_dir}/native_escalation_stage/etc/rc
                COMMAND ${CMAKE_COMMAND} -E env ASTIA_CPIO=${ASTIA_CPIO}
                sh ${CMAKE_CURRENT_SOURCE_DIR}/make_cpio_archive.sh
                ${_fix_dir}/native_escalation_stage ${_fix_dir}/native_escalation_boot_initrd.cpio
                sbin/init sbin/daemond bin/daemonctl bin/stop-client bin/init_libc etc/rc
                lib/libc.so lib/ld.astia.so
                DEPENDS ${_fix_dir}/native_init ${_fix_dir}/native_daemond
                ${_fix_dir}/native_daemondctl ${_fix_dir}/native_stop_client
                ${_init_libc_image} ${ASTIA_KERNEL_SOURCE_DIR}/../daemond/escalation.rc
                ${_fix_dir}/boot_initrd.cpio ${_libc_shared_so}
                astia_userspace_libc_shared
                ${CMAKE_CURRENT_SOURCE_DIR}/make_cpio_archive.sh
                COMMENT "Packing native daemond SIGKILL escalation initrd")
        add_test(NAME astia_native_daemond_escalation
                COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_boot_test.sh
                ${ASTIA_QEMU} $<TARGET_FILE:astiakrn_test>
                ${_fix_dir}/native_escalation_boot_initrd.cpio
                "init: daemond started"
                "daemond: reaped service pid="
                "ignored SIGTERM; sent SIGKILL"
                "daemond: clean shutdown"
                "init: clean shutdown")
        set_tests_properties(astia_native_daemond_escalation PROPERTIES TIMEOUT 300)
        add_custom_target(astia_native_escalation_boot_initrd ALL
                DEPENDS ${_fix_dir}/native_escalation_boot_initrd.cpio)

        add_custom_command(
                OUTPUT ${_fix_dir}/native_failure_boot_initrd.cpio
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/native_failure_stage/sbin
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/native_failure_stage/bin
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/native_failure_stage/etc
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/native_failure_stage/lib
                COMMAND ${CMAKE_COMMAND} -E copy ${_libc_shared_so}
                ${_fix_dir}/native_failure_stage/lib/libc.so
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/ld.astia.so
                ${_fix_dir}/native_failure_stage/lib/ld.astia.so
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
                lib/libc.so lib/ld.astia.so
                DEPENDS ${_fix_dir}/native_init ${_fix_dir}/native_daemond
                ${_fix_dir}/native_daemondctl ${_fix_dir}/native_ps
                ${_init_libc_image} ${ASTIA_KERNEL_SOURCE_DIR}/../daemond/failure.rc
                ${_fix_dir}/boot_initrd.cpio ${_libc_shared_so}
                astia_userspace_libc_shared
                ${CMAKE_CURRENT_SOURCE_DIR}/make_cpio_archive.sh
                COMMENT "Packing failing native daemond initrd")
        add_test(NAME astia_native_daemond_failure
                COMMAND env
                ASTIA_EXPECT_QEMU_STATUS=3
                ASTIA_REQUIRE_SELFTEST=0
                "ASTIA_FORBID_PATTERN=core: selftest ok"
                sh ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_boot_test.sh
                ${ASTIA_QEMU} $<TARGET_FILE:astiakrn_test>
                ${_fix_dir}/native_failure_boot_initrd.cpio
                "daemond: malformed service record"
                "init: daemond exited status="
                "init: daemond failed")
        set_tests_properties(astia_native_daemond_failure PROPERTIES TIMEOUT 300)
        add_custom_target(astia_native_failure_boot_initrd ALL
                DEPENDS ${_fix_dir}/native_failure_boot_initrd.cpio)

        # Visibility: configured services run ps and daemonctl, so the boot
        # log proves the supervisor is one visible process with PPID 1 and
        # that the control socket reaches it.
        add_custom_command(
                OUTPUT ${_fix_dir}/native_visibility_boot_initrd.cpio
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/native_visibility_stage/sbin
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/native_visibility_stage/bin
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/native_visibility_stage/etc
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/native_visibility_stage/lib
                COMMAND ${CMAKE_COMMAND} -E copy ${_libc_shared_so}
                ${_fix_dir}/native_visibility_stage/lib/libc.so
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/ld.astia.so
                ${_fix_dir}/native_visibility_stage/lib/ld.astia.so
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/native_init ${_fix_dir}/native_visibility_stage/sbin/init
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/native_daemond ${_fix_dir}/native_visibility_stage/sbin/daemond
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/native_daemondctl ${_fix_dir}/native_visibility_stage/bin/daemonctl
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/native_ps ${_fix_dir}/native_visibility_stage/bin/ps
                COMMAND ${CMAKE_COMMAND} -E copy ${_init_libc_image} ${_fix_dir}/native_visibility_stage/bin/init_libc
                COMMAND ${CMAKE_COMMAND} -E copy ${ASTIA_KERNEL_SOURCE_DIR}/../daemond/visibility.rc ${_fix_dir}/native_visibility_stage/etc/rc
                COMMAND ${CMAKE_COMMAND} -E env ASTIA_CPIO=${ASTIA_CPIO}
                sh ${CMAKE_CURRENT_SOURCE_DIR}/make_cpio_archive.sh
                ${_fix_dir}/native_visibility_stage ${_fix_dir}/native_visibility_boot_initrd.cpio
                sbin/init sbin/daemond bin/daemonctl bin/ps bin/init_libc etc/rc
                lib/libc.so lib/ld.astia.so
                DEPENDS ${_fix_dir}/native_init ${_fix_dir}/native_daemond
                ${_fix_dir}/native_daemondctl ${_fix_dir}/native_ps
                ${_init_libc_image} ${ASTIA_KERNEL_SOURCE_DIR}/../daemond/visibility.rc
                ${_fix_dir}/boot_initrd.cpio ${_libc_shared_so}
                astia_userspace_libc_shared
                ${CMAKE_CURRENT_SOURCE_DIR}/make_cpio_archive.sh
                COMMENT "Packing native daemond visibility initrd")
        add_test(NAME astia_native_daemond_visibility
                COMMAND env
                "ASTIA_EXPECT_ONCE_PATTERN=daemond: started"
                "ASTIA_EXPECT_ONCE_REGEX=^[0-9]+[[:space:]]+1[[:space:]]+.*[[:space:]]/sbin/daemond[[:space:]]*$"
                sh ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_boot_test.sh
                ${ASTIA_QEMU} $<TARGET_FILE:astiakrn_test>
                ${_fix_dir}/native_visibility_boot_initrd.cpio
                "PID    PPID   PGID   UID    GID    STAT CTTY       COMMAND"
                "services=2"
                "service=/bin/daemonctl state=running"
                "daemond: reaped service pid="
                "daemond: clean shutdown"
                "init: clean shutdown")
        set_tests_properties(astia_native_daemond_visibility PROPERTIES TIMEOUT 300)

        # A missing rc must be reported with its errno and must not terminate
        # the supervisor: PID 1 still has a live child.
        add_custom_command(
                OUTPUT ${_fix_dir}/native_missing_rc_boot_initrd.cpio
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/native_missing_rc_stage/sbin
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/native_missing_rc_stage/bin
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/native_missing_rc_stage/lib
                COMMAND ${CMAKE_COMMAND} -E copy ${_libc_shared_so}
                ${_fix_dir}/native_missing_rc_stage/lib/libc.so
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/ld.astia.so
                ${_fix_dir}/native_missing_rc_stage/lib/ld.astia.so
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/native_init ${_fix_dir}/native_missing_rc_stage/sbin/init
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/native_daemond ${_fix_dir}/native_missing_rc_stage/sbin/daemond
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/native_daemondctl ${_fix_dir}/native_missing_rc_stage/bin/daemonctl
                COMMAND ${CMAKE_COMMAND} -E env ASTIA_CPIO=${ASTIA_CPIO}
                sh ${CMAKE_CURRENT_SOURCE_DIR}/make_cpio_archive.sh
                ${_fix_dir}/native_missing_rc_stage ${_fix_dir}/native_missing_rc_boot_initrd.cpio
                sbin/init sbin/daemond bin/daemonctl
                lib/libc.so lib/ld.astia.so
                DEPENDS ${_fix_dir}/native_init ${_fix_dir}/native_daemond
                ${_fix_dir}/native_daemondctl ${_fix_dir}/boot_initrd.cpio
                ${_libc_shared_so} astia_userspace_libc_shared
                ${CMAKE_CURRENT_SOURCE_DIR}/make_cpio_archive.sh
                COMMENT "Packing native daemond missing-rc initrd")
        add_test(NAME astia_native_daemond_missing_rc
                COMMAND env
                ASTIA_EXPECT_TIMEOUT=1
                ASTIA_REQUIRE_SELFTEST=0
                ASTIA_BOOT_QEMU_TIMEOUT=6
                sh ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_boot_test.sh
                ${ASTIA_QEMU} $<TARGET_FILE:astiakrn_test>
                ${_fix_dir}/native_missing_rc_boot_initrd.cpio
                "init: daemond started"
                "daemond: open /etc/rc failed: errno=2; starting with no services"
                "daemond: no services configured; idling")
        set_tests_properties(astia_native_daemond_missing_rc PROPERTIES TIMEOUT 60)

        # Launch failure is diagnosed by init itself, before the kernel panics.
        add_custom_command(
                OUTPUT ${_fix_dir}/native_launch_failure_boot_initrd.cpio
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/native_launch_failure_stage/sbin
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/native_launch_failure_stage/lib
                COMMAND ${CMAKE_COMMAND} -E copy ${_libc_shared_so}
                ${_fix_dir}/native_launch_failure_stage/lib/libc.so
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/ld.astia.so
                ${_fix_dir}/native_launch_failure_stage/lib/ld.astia.so
                COMMAND ${CMAKE_COMMAND} -E copy ${_fix_dir}/native_init ${_fix_dir}/native_launch_failure_stage/sbin/init
                COMMAND ${CMAKE_COMMAND} -E env ASTIA_CPIO=${ASTIA_CPIO}
                sh ${CMAKE_CURRENT_SOURCE_DIR}/make_cpio_archive.sh
                ${_fix_dir}/native_launch_failure_stage ${_fix_dir}/native_launch_failure_boot_initrd.cpio
                sbin/init
                lib/libc.so lib/ld.astia.so
                DEPENDS ${_fix_dir}/native_init ${_fix_dir}/boot_initrd.cpio
                ${_libc_shared_so} astia_userspace_libc_shared
                ${CMAKE_CURRENT_SOURCE_DIR}/make_cpio_archive.sh
                COMMENT "Packing native init launch-failure initrd")
        add_test(NAME astia_native_daemond_launch_failure
                COMMAND env
                ASTIA_EXPECT_QEMU_STATUS=3
                ASTIA_REQUIRE_SELFTEST=0
                "ASTIA_FORBID_PATTERN=init: daemond started"
                sh ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_boot_test.sh
                ${ASTIA_QEMU} $<TARGET_FILE:astiakrn_test>
                ${_fix_dir}/native_launch_failure_boot_initrd.cpio
                "init: cannot start /sbin/daemond: errno=2")
        set_tests_properties(astia_native_daemond_launch_failure PROPERTIES TIMEOUT 300)

        add_custom_target(astia_native_initrd_fixtures ALL
                DEPENDS ${_fix_dir}/native_boot_initrd.cpio
                ${_fix_dir}/native_service_boot_initrd.cpio
                ${_fix_dir}/native_failure_boot_initrd.cpio
                ${_fix_dir}/native_visibility_boot_initrd.cpio
                ${_fix_dir}/native_missing_rc_boot_initrd.cpio
                ${_fix_dir}/native_launch_failure_boot_initrd.cpio)

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

        if (GRUB_FILE AND GRUB_MKRESCUE AND XORRISO)
            add_custom_target(astia_acpi_grub_iso
                    COMMAND ${CMAKE_COMMAND} -E make_directory
                            ${CMAKE_BINARY_DIR}/acpi-grub/boot/grub
                    COMMAND ${CMAKE_COMMAND} -E copy
                            $<TARGET_FILE:astiakrn_acpi>
                            ${CMAKE_BINARY_DIR}/acpi-grub/boot/astiakrn
                    COMMAND ${CMAKE_COMMAND} -E copy
                            ${ASTIA_KERNEL_SOURCE_DIR}/grub.cfg
                            ${CMAKE_BINARY_DIR}/acpi-grub/boot/grub/grub.cfg
                    COMMAND ${GRUB_FILE} --is-x86-multiboot2
                            $<TARGET_FILE:astiakrn_acpi>
                    COMMAND ${GRUB_MKRESCUE} -o ${CMAKE_BINARY_DIR}/astia-acpi.iso
                            ${CMAKE_BINARY_DIR}/acpi-grub
                    BYPRODUCTS ${CMAKE_BINARY_DIR}/astia-acpi.iso
                    DEPENDS astiakrn_acpi
                    COMMENT "Building ACPI GRUB Multiboot2 ISO")

            add_test(NAME acpi_q35_shutdown
                    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_acpi_q35.sh
                    shutdown ${ASTIA_QEMU} ${CMAKE_BINARY_DIR}/astia-acpi.iso)
            set_tests_properties(acpi_q35_shutdown PROPERTIES
                    TIMEOUT 300
                    LABELS "acpi;qemu;deterministic"
                    RESOURCE_LOCK astia-qemu
                    RUN_SERIAL TRUE)

            add_test(NAME acpi_q35_malformed_rsdp
                    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_acpi_q35.sh
                    reject-rsdp ${ASTIA_QEMU}
                    $<TARGET_FILE:astiakrn_acpi_reject>)
            set_tests_properties(acpi_q35_malformed_rsdp PROPERTIES
                    TIMEOUT 40
                    LABELS "acpi;qemu;deterministic"
                    RESOURCE_LOCK astia-qemu
                    RUN_SERIAL TRUE)

            add_test(NAME acpi_q35_malformed_aml
                    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_acpi_q35.sh
                    reject-aml ${ASTIA_QEMU}
                    $<TARGET_FILE:astiakrn_acpi_reject>)
            set_tests_properties(acpi_q35_malformed_aml PROPERTIES
                    TIMEOUT 40
                    LABELS "acpi;qemu;deterministic"
                    RESOURCE_LOCK astia-qemu
                    RUN_SERIAL TRUE)
        endif ()
    else ()
        message(STATUS "astia: qemu-system-x86_64 not found; skipping astia_kernel_boot")
    endif ()
endif ()

astia_append_test_label("kernel;deterministic")
foreach (_astia_boot_test IN ITEMS
        astia_kernel_boot astia_fs_registry_boot astia_kernel_lifecycle_smp2
        astia_kernel_smp_boot astia_kernel_scheduler_smp2 astia_initrd_boot
        astia_module_syscall_boot astia_urandom_fresh_boot
        astia_production_init_exit
        astia_native_daemond_idle astia_native_daemond_service
        astia_native_daemond_visibility astia_native_daemond_missing_rc
        astia_native_daemond_launch_failure astia_native_daemond_failure
        astia_native_daemond_escalation)
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
