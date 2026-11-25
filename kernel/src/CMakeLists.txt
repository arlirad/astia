# Core source directories
if (ARCH STREQUAL "aarch64")
    # The generic full kernel graph (paging, IRQ, devices, scheduler) has no
    # AArch64 production implementation yet.  This stage owns its complete
    # source composition so x86 keeps the existing target graph unchanged.
    add_subdirectory(lib)
    add_subdirectory(arch)
    add_subdirectory(plat/aarch64-virt)
    add_subdirectory(boot/aarch64-virt)
    return()
endif ()

add_subdirectory(lib)
add_subdirectory(net)
add_subdirectory(io)
add_subdirectory(ipc)
add_subdirectory(dbg)
add_subdirectory(core)
add_subdirectory(mm)
add_subdirectory(fs)
add_subdirectory(proc)
add_subdirectory(dl)
add_subdirectory(sys)
add_subdirectory(util)
add_subdirectory(hw)
add_subdirectory(dev)
add_subdirectory(log)
add_subdirectory(elf)
add_subdirectory(initrd)
add_subdirectory(mod)
add_subdirectory(init)
add_subdirectory(boot)
add_subdirectory(arch)
add_subdirectory(plat)

set(_astia_kernel_libs
        -Wl,--whole-archive
        init
        net
        io
        fs
        core
        proc
        dl
        sys
        arch_${ARCH}
        mm
        hw
        dev
        log
        elf
        initrd
        mod
        boot_${BOOT}
        plat_${PLATFORM}
        util
        libcore
        libcore_freestanding
        dbg
        -Wl,--no-whole-archive
)

function(astia_configure_kernel target)
    target_include_directories(${target} PRIVATE
            ${ASTIA_KERNEL_SOURCE_DIR}/include
            ${ASTIA_KERNEL_SOURCE_DIR}/src/test
    )
    target_link_libraries(${target} PRIVATE ${_astia_kernel_libs})
    target_link_options(${target} PRIVATE
            -T ${ASTIA_LINKER_SCRIPT}
    )
endfunction()

# Production kernel entrypoint: initialization only. Hardware assertions
# live in astiakrn_test, which is what the QEMU CTests boot.
add_executable(astiakrn kmain.cpp)
astia_configure_kernel(astiakrn)

function(astia_generate_module_symbols target)
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
            ${ASTIA_LINKER_SCRIPT}
            ${ASTIA_KERNEL_SOURCE_DIR}/scripts/generate_module_symbols.py)
    add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${Python3_EXECUTABLE}
                    ${ASTIA_KERNEL_SOURCE_DIR}/scripts/generate_module_symbols.py
                    $<TARGET_FILE:${target}>
            VERBATIM)
endfunction()
astia_generate_module_symbols(astiakrn)

if (ARCH STREQUAL "x86_64")
    set(ASTIA_SELFTEST_ARCH_SOURCE test/selftest_x86_64.cpp)
else ()
    message(FATAL_ERROR "No production self-test policy for ARCH=${ARCH}")
endif ()

add_executable(astiakrn_test
        test_kmain.cpp
        test/selftest.cpp
        ${ASTIA_SELFTEST_ARCH_SOURCE}
)
astia_configure_kernel(astiakrn_test)
target_compile_definitions(astiakrn_test PRIVATE ASTIA_RUN_SELFTEST=1)
astia_generate_module_symbols(astiakrn_test)

add_executable(astiakrn_runtime
        test_kmain.cpp
)
astia_configure_kernel(astiakrn_runtime)
astia_generate_module_symbols(astiakrn_runtime)
target_compile_definitions(astiakrn_runtime PRIVATE ASTIA_RUN_SELFTEST=0)
option(ASTIA_RUNTIME_SKIP_LIFECYCLE "Skip userspace lifecycle for module-only runtime fixtures" OFF)
if (ASTIA_RUNTIME_SKIP_LIFECYCLE)
    target_compile_definitions(astiakrn_runtime PRIVATE ASTIA_RUN_LIFECYCLE=0)
endif ()

add_executable(astiakrn_module_runtime
        test_kmain.cpp
)
astia_configure_kernel(astiakrn_module_runtime)
astia_generate_module_symbols(astiakrn_module_runtime)
target_compile_definitions(astiakrn_module_runtime PRIVATE
        ASTIA_RUN_SELFTEST=0 ASTIA_RUN_LIFECYCLE=0)
