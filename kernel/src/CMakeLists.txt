# Core source directories
add_subdirectory(lib)
add_subdirectory(ipc)
add_subdirectory(dbg)
add_subdirectory(core)
add_subdirectory(mm)
add_subdirectory(fs)
add_subdirectory(proc)
add_subdirectory(sys)
add_subdirectory(util)
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
        fs
        core
        proc
        sys
        arch_${ARCH}
        mm
        dev
        log
        elf
        initrd
        mod
        boot_${BOOT}
        plat_${PLATFORM}
        util
        libcore
        dbg
        -Wl,--no-whole-archive
)

function(astia_configure_kernel target)
    target_include_directories(${target} PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/src/test
    )
    target_link_libraries(${target} PRIVATE ${_astia_kernel_libs})
    target_link_options(${target} PRIVATE
            -T ${CMAKE_SOURCE_DIR}/cmake/linker/virt-x86_64.ld
    )
endfunction()

# Production kernel entrypoint: initialization only. Hardware assertions
# live in astiakrn_test, which is what the QEMU CTests boot.
add_executable(astiakrn kmain.cpp)
astia_configure_kernel(astiakrn)

add_executable(astiakrn_test
        test_kmain.cpp
        test/selftest.cpp
)
astia_configure_kernel(astiakrn_test)
target_compile_definitions(astiakrn_test PRIVATE ASTIA_RUN_SELFTEST=1)

add_executable(astiakrn_runtime
        test_kmain.cpp
)
astia_configure_kernel(astiakrn_runtime)
target_compile_definitions(astiakrn_runtime PRIVATE ASTIA_RUN_SELFTEST=0)
