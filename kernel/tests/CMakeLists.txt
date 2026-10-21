# Host-side unit tests. Unlike everything else in the tree these build with the
# *system* toolchain and link against the host C library, so undo the
# freestanding compile/link settings inherited from the parent directory.
set(CMAKE_CXX_FLAGS "-Wall -Wextra")
set_property(DIRECTORY PROPERTY LINK_OPTIONS "")
option(ASTIA_CROSS_USERSPACE "Use the Clang Astia sysroot for init_libc" OFF)

add_library(astia_test_support STATIC
        support/main.cpp
        support/arena.cpp
)
target_include_directories(astia_test_support PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/support
        ${CMAKE_SOURCE_DIR}/include
)

# The real kernel sources are compiled straight into the test binaries, so the
# tests exercise shipping code rather than copies.
set(_astia_test_includes
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/lib
        ${CMAKE_SOURCE_DIR}/src/ipc
        ${CMAKE_SOURCE_DIR}/src/mm
        ${CMAKE_SOURCE_DIR}/src/elf
        ${CMAKE_SOURCE_DIR}/src/initrd
        ${CMAKE_SOURCE_DIR}/src/mod
        ${CMAKE_SOURCE_DIR}/src/boot/virt
        ${CMAKE_SOURCE_DIR}/src/arch
        ${CMAKE_SOURCE_DIR}/src/arch/x86_64
)

function(astia_add_host_test name)
    add_executable(${name} ${ARGN})
    set_target_properties(${name} PROPERTIES LINK_OPTIONS "")
    target_link_libraries(${name} PRIVATE astia_test_support)
    target_include_directories(${name} PRIVATE ${_astia_test_includes})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# --- Pure logic: no hardware, no fake memory needed. ------------------------
astia_add_host_test(astia_memory_map_test memory_map_test.cpp)
astia_add_host_test(astia_pvh_test pvh_test.cpp ${CMAKE_SOURCE_DIR}/src/boot/virt/pvh.cpp)

# error_t / optional<T>: header-only carriers for fallible kernel operations.
astia_add_host_test(astia_optional_test optional_test.cpp)
astia_add_host_test(astia_common_test common_test.cpp)
astia_add_host_test(astia_signal_test signal_test.cpp)
astia_add_host_test(astia_process_group_test process_group_test.cpp)
astia_add_host_test(astia_tty_test
        tty_test.cpp
        ${CMAKE_SOURCE_DIR}/src/dev/chr/tty.cpp
        ${CMAKE_SOURCE_DIR}/src/dev/device.cpp
)
target_include_directories(astia_tty_test PRIVATE ${CMAKE_SOURCE_DIR}/src/dev)

find_package(Threads REQUIRED)
astia_add_host_test(astia_mpmc_queue_test mpmc_queue_test.cpp)
target_link_libraries(astia_mpmc_queue_test PRIVATE Threads::Threads)

# --- ELF fixtures: compile real images with the host toolchain and byte-embed
# them, so the loader sees genuine gcc output rather than hand-written ELF.
set(_fix_dir ${CMAKE_CURRENT_BINARY_DIR}/fixtures)

# Embed an object as a C++ TU exposing `unsigned char <blob_name>[]` + `_len`.
function(astia_embed_blob out_cpp obj_path blob_name)
    set(_cpp ${obj_path}.cpp)
    add_custom_command(
            OUTPUT ${_cpp}
            COMMAND sh -c "xxd -i -n ${blob_name} ${obj_path} > ${_cpp}"
            DEPENDS ${obj_path}
            COMMENT "Embedding ${blob_name}"
            VERBATIM
    )
    set(${out_cpp} ${_cpp} PARENT_SCOPE)
endfunction()

# ET_REL module object (relocatable): imports kernel_add, has module_main.
add_custom_command(
        OUTPUT ${_fix_dir}/hello_mod.o
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}
        COMMAND ${CMAKE_C_COMPILER} -c -ffreestanding -fno-pic -fno-pie
        -fno-stack-protector -fno-asynchronous-unwind-tables
        -o ${_fix_dir}/hello_mod.o ${CMAKE_CURRENT_SOURCE_DIR}/hello_mod.c
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/hello_mod.c
        COMMENT "Compiling ET_REL fixture hello_mod.o"
)
astia_embed_blob(_hello_mod_cpp ${_fix_dir}/hello_mod.o hello_mod_blob)

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
        COMMAND ${CMAKE_C_COMPILER} -no-pie -nostdlib -O0
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
        ${_fixture_dyn_cpp}
        ${_fixture_exec_cpp}
        ${CMAKE_SOURCE_DIR}/src/elf/elf.cpp
)

# initrd: the ustar reader, against a tarball built by make_initrd.sh (three
# regular files, one binary, plus a `sub/` directory entry to be skipped).
add_custom_command(
        OUTPUT ${_fix_dir}/initrd.tar
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/make_initrd.sh ${_fix_dir}/initrd.tar
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/make_initrd.sh
        COMMENT "Building initrd fixture initrd.tar"
)
astia_embed_blob(_initrd_cpp ${_fix_dir}/initrd.tar initrd_blob)

astia_add_host_test(astia_initrd_test
        initrd_test.cpp
        ${_initrd_cpp}
        ${CMAKE_SOURCE_DIR}/src/initrd/initrd.cpp
)

# mod: the export table (the ELF loader itself is covered by astia_elf_test).
astia_add_host_test(astia_mod_test
        mod_test.cpp
        ${CMAKE_SOURCE_DIR}/src/mod/exports.cpp
)

# memory.cpp is the code under test here, so it overrides the host libc's
# symbols; -fno-builtin stops the compiler rewriting our loops back into calls.
astia_add_host_test(astia_memory_test memory_test.cpp ${CMAKE_SOURCE_DIR}/src/lib/memory.cpp)
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/lib/memory.cpp
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
            "-no-pie"
            "-Wl,--defsym=__kernel_start=0x08000000"
            "-Wl,--defsym=__kernel_end=0x08002000"
    )
    # Compiles out the privileged invlpg / mov cr3 in page_map.cpp.
    target_compile_definitions(${name} PRIVATE ASTIA_HOST_TEST=1)
endfunction()

astia_add_arena_test(astia_frame_allocator_test
        frame_allocator_test.cpp
        ${CMAKE_SOURCE_DIR}/src/mm/frame_allocator.cpp
)
astia_add_arena_test(astia_page_map_test
        page_map_test.cpp
        ${CMAKE_SOURCE_DIR}/src/arch/x86_64/page_map.cpp
        ${CMAKE_SOURCE_DIR}/src/arch/x86_64/layout.cpp
        ${CMAKE_SOURCE_DIR}/src/mm/frame_allocator.cpp
)

# mm::heap: the free-list allocator over a window backed by arena frames.
astia_add_arena_test(astia_heap_test
        heap_test.cpp
        ${CMAKE_SOURCE_DIR}/src/mm/heap.cpp
        ${CMAKE_SOURCE_DIR}/src/mm/frame_allocator.cpp
)

# mm::phys_region: region-local reference counting and the CoW
# parent/child links — no kernel-wide per-frame table anywhere.
astia_add_arena_test(astia_phys_region_test
        phys_region_test.cpp
        ${CMAKE_SOURCE_DIR}/src/mm/phys_region.cpp
        ${CMAKE_SOURCE_DIR}/src/mm/pinned_buffer.cpp
        ${CMAKE_SOURCE_DIR}/src/mm/iovec.cpp
        ${CMAKE_SOURCE_DIR}/src/mm/heap.cpp
        ${CMAKE_SOURCE_DIR}/src/mm/frame_allocator.cpp
)

# On-target integration test: boot the real kernel under QEMU and let its
# isa-debug-exit status decide pass/fail. Covers what host tests cannot (CR3,
# TLB, real physical memory, the boot path itself).
if (ARCH STREQUAL "x86_64")
    find_program(ASTIA_QEMU qemu-system-x86_64)
    if (ASTIA_QEMU)
        add_test(NAME astia_kernel_boot
                COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_boot_test.sh
                ${ASTIA_QEMU} $<TARGET_FILE:astiakrn_test>
                ""
                "selftest: interrupts ok (lapic)"
                "selftest: clock ok"
                "selftest: statx ok"
        )
        set_tests_properties(astia_kernel_boot PROPERTIES TIMEOUT 60)

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
        )
        set_tests_properties(astia_kernel_lifecycle_smp2 PROPERTIES
                TIMEOUT 60 ENVIRONMENT "ASTIA_QEMU_SMP=2")

        # End-to-end initrd: the same kernel, but the boot module is a tarball
        # carrying a self-contained ET_REL module. QEMU's PVH path puts -initrd in
        # the modlist, so this covers modlist -> tar walk -> ELF load -> run.
        add_custom_command(
                OUTPUT ${_fix_dir}/hello_initrd.o
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}
                COMMAND ${CMAKE_C_COMPILER} -c -ffreestanding -fno-pic -fno-pie
                -fno-stack-protector -fno-asynchronous-unwind-tables
                -o ${_fix_dir}/hello_initrd.o ${CMAKE_CURRENT_SOURCE_DIR}/hello_initrd.c
                DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/hello_initrd.c
                COMMENT "Compiling ET_REL fixture hello_initrd.o"
        )
        # The libc in ring 3: the same sources the host tests link, but
        # freestanding and non-PIE, with the AIO ports as Astia's side of the
        # seams.  Userspace images link this against tests/crt0.S.
        add_library(astia_userspace_libc STATIC
                ${CMAKE_SOURCE_DIR}/../libc/src/assert.c
                ${CMAKE_SOURCE_DIR}/../libc/src/backend_aio.c
                ${CMAKE_SOURCE_DIR}/../libc/src/ctype.c
                ${CMAKE_SOURCE_DIR}/../libc/src/dirent.c
                ${CMAKE_SOURCE_DIR}/../libc/src/errno.c
                ${CMAKE_SOURCE_DIR}/../libc/src/file.c
                ${CMAKE_SOURCE_DIR}/../libc/src/group.c
                ${CMAKE_SOURCE_DIR}/../libc/src/ioctl.c
                ${CMAKE_SOURCE_DIR}/../libc/src/locale.c
                ${CMAKE_SOURCE_DIR}/../libc/src/malloc.c
                ${CMAKE_SOURCE_DIR}/../libc/src/math.c
                ${CMAKE_SOURCE_DIR}/../libc/src/mman.c
                ${CMAKE_SOURCE_DIR}/../libc/src/process.c
                ${CMAKE_SOURCE_DIR}/../libc/src/process_aio.c
                ${CMAKE_SOURCE_DIR}/../libc/src/prctl.c
                ${CMAKE_SOURCE_DIR}/../libc/src/pthread.c
                ${CMAKE_SOURCE_DIR}/../libc/src/printf.c
                ${CMAKE_SOURCE_DIR}/../libc/src/rlimit.c
                ${CMAKE_SOURCE_DIR}/../libc/src/scanf.c
                ${CMAKE_SOURCE_DIR}/../libc/src/setjmp.c
                ${CMAKE_SOURCE_DIR}/../libc/src/signal.c
                ${CMAKE_SOURCE_DIR}/../libc/src/snprintf.c
                ${CMAKE_SOURCE_DIR}/../libc/src/stdlib.c
                ${CMAKE_SOURCE_DIR}/../libc/src/stat.c
                ${CMAKE_SOURCE_DIR}/../libc/src/string.c
                ${CMAKE_SOURCE_DIR}/../libc/src/termcap.c
                ${CMAKE_SOURCE_DIR}/../libc/src/termios.c
                ${CMAKE_SOURCE_DIR}/../libc/src/time.c
                ${CMAKE_SOURCE_DIR}/../libc/src/time_aio.c
                ${CMAKE_SOURCE_DIR}/../libc/src/times.c
                ${CMAKE_SOURCE_DIR}/../libc/src/unistd.c
                ${CMAKE_SOURCE_DIR}/../libc/src/utsname.c
                ${CMAKE_SOURCE_DIR}/../libc/src/uio.c
                ${CMAKE_SOURCE_DIR}/../libc/src/wchar.c
        )
        target_include_directories(astia_userspace_libc PRIVATE
                ${CMAKE_SOURCE_DIR}/../libc/include
                ${CMAKE_SOURCE_DIR}/include
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
        if (ASTIA_CROSS_USERSPACE AND ASTIA_CLANG)
            set(_cross_dir ${_fix_dir}/clang)
            file(GLOB _cross_libc_sources CONFIGURE_DEPENDS
                    ${CMAKE_SOURCE_DIR}/../libc/src/*.c)
            add_custom_command(
                    OUTPUT ${_cross_dir}/image/init_libc
                    COMMAND ${CMAKE_COMMAND} -E env
                    ASTIA_CROSS_BUILD=${_cross_dir}
                    ASTIA_SYSROOT=${_cross_dir}/sysroot
                    CLANG=${ASTIA_CLANG}
                    ${CMAKE_SOURCE_DIR}/../toolchain/build-init-libc.sh
                    DEPENDS
                    ${CMAKE_SOURCE_DIR}/../toolchain/build-init-libc.sh
                    ${CMAKE_SOURCE_DIR}/../toolchain/astia-x86_64-clang.cmake
                    ${CMAKE_CURRENT_SOURCE_DIR}/crt0.S
                    ${CMAKE_CURRENT_SOURCE_DIR}/init_libc.c
                    ${_cross_libc_sources}
                    ${CMAKE_SOURCE_DIR}/../libc/CMakeLists.txt
                    COMMENT "Cross-building libc-backed userspace image init_libc"
            )
            set(_init_libc_image ${_cross_dir}/image/init_libc)
        else ()
            add_custom_command(
                    OUTPUT ${_fix_dir}/init_libc
                    COMMAND ${CMAKE_C_COMPILER} -c -ffreestanding -fno-pic -fno-pie
                    -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone
                    -o ${_fix_dir}/crt0.o ${CMAKE_CURRENT_SOURCE_DIR}/crt0.S
                    COMMAND ${CMAKE_C_COMPILER} -c -ffreestanding -fno-pic -fno-pie
                    -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone
                    -fno-builtin -I${CMAKE_SOURCE_DIR}/include
                    -I${CMAKE_SOURCE_DIR}/../libc/include
                    -o ${_fix_dir}/init_libc_main.o
                    ${CMAKE_CURRENT_SOURCE_DIR}/init_libc.c
                    COMMAND ${CMAKE_C_COMPILER} -nostdlib -static -no-pie
                    -Wl,-e,_start -Wl,-Ttext-segment=0x40000000 -lgcc
                    -o ${_fix_dir}/init_libc ${_fix_dir}/crt0.o
                    ${_fix_dir}/init_libc_main.o $<TARGET_FILE:astia_userspace_libc>
                    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/crt0.S
                    ${CMAKE_CURRENT_SOURCE_DIR}/init_libc.c astia_userspace_libc
                    COMMENT "Linking libc-backed userspace image init_libc"
            )
            set(_init_libc_image ${_fix_dir}/init_libc)
        endif ()

    add_custom_command(
            OUTPUT ${_fix_dir}/boot_initrd.tar
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/boot_stage
                COMMAND ${CMAKE_COMMAND} -E copy
                ${_fix_dir}/hello_initrd.o ${_fix_dir}/boot_stage/
                COMMAND ${CMAKE_C_COMPILER} -c -ffreestanding -fno-pic -fno-pie
                -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone
                -I${CMAKE_SOURCE_DIR}/include
                -o ${_fix_dir}/init_user.o ${CMAKE_CURRENT_SOURCE_DIR}/init_user.c
                COMMAND ${CMAKE_C_COMPILER} -nostdlib -static -no-pie
                -Wl,-e,_start -Wl,-Ttext-segment=0x40000000
                -o ${_fix_dir}/init_user ${_fix_dir}/init_user.o
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/boot_stage/sbin
                COMMAND ${CMAKE_COMMAND} -E copy
                ${_fix_dir}/init_user ${_fix_dir}/boot_stage/sbin/init
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_fix_dir}/boot_stage/bin
                COMMAND ${CMAKE_COMMAND} -E copy
                ${_init_libc_image} ${_fix_dir}/boot_stage/bin/init_libc
                COMMAND sh -c "tar cf ${_fix_dir}/boot_initrd.tar -C ${_fix_dir}/boot_stage hello_initrd.o sbin/init bin/init_libc"
                DEPENDS ${_fix_dir}/hello_initrd.o ${CMAKE_CURRENT_SOURCE_DIR}/init_user.c ${_init_libc_image}
                COMMENT "Packing boot initrd boot_initrd.tar"
        )
        # Tarballs are test inputs, not sources of any target, so build them as
        # part of the default build to guarantee they exist before ctest runs.
        add_custom_target(astia_boot_initrd ALL DEPENDS ${_fix_dir}/boot_initrd.tar)

        add_test(NAME astia_initrd_boot
                COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_boot_test.sh
                ${ASTIA_QEMU} $<TARGET_FILE:astiakrn_test>
                ${_fix_dir}/boot_initrd.tar
                "mod: hello_initrd.o rc=42"
                "selftest: statx ok"
                "user thread exit pid="
                "astia: libc ok"
        )
        set_tests_properties(astia_initrd_boot PROPERTIES TIMEOUT 60)
    else ()
        message(STATUS "astia: qemu-system-x86_64 not found; skipping astia_kernel_boot")
    endif ()
endif ()
