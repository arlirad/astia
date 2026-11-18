# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Arlirad

cmake_minimum_required(VERSION 3.20)

# The repository-level project is the canonical entry point for IDEs.  The
# kernel remains independently configurable from kernel/, while libc keeps
# its separate freestanding toolchain and install/sysroot workflow.
project(astia LANGUAGES NONE)

enable_testing()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/AstiaCTest.cmake)

add_test(NAME astia_license_metadata
        COMMAND ${CMAKE_COMMAND} -P
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/license_metadata_check.cmake)
add_test(NAME astia_vga_launcher_defaults
        COMMAND ${CMAKE_COMMAND} -P
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/vga_launcher_defaults_check.cmake)
add_test(NAME astia_direct_vga_gdb_smoke
        COMMAND ${CMAKE_COMMAND} -P
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/direct_vga_gdb_smoke.cmake)
set_tests_properties(astia_license_metadata astia_vga_launcher_defaults
        astia_direct_vga_gdb_smoke
        PROPERTIES LABELS "repository;deterministic" TIMEOUT 30)

option(ASTIA_BUILD_TESTS
        "Build and register deterministic host-side tests" ON)
option(ASTIA_ENABLE_EXTERNAL_TESTS
        "Enable CTest suites requiring external source trees or network access" OFF)
option(ASTIA_ENABLE_MANUAL_TESTS
        "Enable CTest suites that require a graphical or interactive host" OFF)
option(ASTIA_CROSS_USERSPACE
        "Build the Clang cross libc image used by userspace CTest suites" ON)
set(ASTIA_ZSH_SOURCE "$ENV{ZSH_SOURCE}" CACHE PATH
        "Clean upstream zsh source tree for external CTest suites")
set(ASTIA_POSIX_TEST_SOURCE "$ENV{POSIX_TEST_SOURCE}" CACHE PATH
        "Clean LTP Open POSIX source tree for external CTest suites")

set(ASTIA_VGA_QEMU "$ENV{QEMU}" CACHE STRING
    "QEMU executable used by the Astia VGA launcher")
if(ASTIA_VGA_QEMU STREQUAL "")
    set(ASTIA_VGA_QEMU "qemu-system-x86_64" CACHE STRING
        "QEMU executable used by the Astia VGA launcher" FORCE)
endif()
set(ASTIA_VGA_MEMORY "$ENV{ASTIA_VGA_MEMORY}" CACHE STRING
    "QEMU memory size for the Astia VGA launcher")
if(ASTIA_VGA_MEMORY STREQUAL "")
    set(ASTIA_VGA_MEMORY "256M" CACHE STRING
        "QEMU memory size for the Astia VGA launcher" FORCE)
endif()
set(ASTIA_VGA_CPUS "$ENV{ASTIA_VGA_CPUS}" CACHE STRING
    "QEMU virtual CPU count for the Astia VGA launcher")
if(ASTIA_VGA_CPUS STREQUAL "")
    set(ASTIA_VGA_CPUS "2" CACHE STRING
        "QEMU virtual CPU count for the Astia VGA launcher" FORCE)
endif()
set(ASTIA_VGA_NANO "$ENV{ASTIA_NANO}" CACHE BOOL
    "Build and include nano in the Astia VGA launcher image")
if(ASTIA_VGA_NANO STREQUAL "")
    set(ASTIA_VGA_NANO OFF CACHE BOOL
        "Build and include nano in the Astia VGA launcher image" FORCE)
endif()
set(ASTIA_VGA_ZSH_BUILD "$ENV{ZSH_BUILD}" CACHE PATH
    "Reusable zsh build directory for the Astia VGA launcher")
if(ASTIA_VGA_ZSH_BUILD STREQUAL "")
    set(ASTIA_VGA_ZSH_BUILD "${CMAKE_BINARY_DIR}/zsh" CACHE PATH
        "Reusable zsh build directory for the Astia VGA launcher" FORCE)
endif()
set(ASTIA_VGA_KERNEL_BUILD "$ENV{ASTIA_KERNEL_BUILD}" CACHE PATH
    "Kernel build directory for the Astia VGA launcher")
if(ASTIA_VGA_KERNEL_BUILD STREQUAL "")
    set(ASTIA_VGA_KERNEL_BUILD "${CMAKE_BINARY_DIR}/kernel" CACHE PATH
        "Kernel build directory for the Astia VGA launcher" FORCE)
endif()
set(ASTIA_VGA_NANO_BUILD "$ENV{ASTIA_NANO_BUILD}" CACHE PATH
    "Nano build directory for the Astia VGA launcher")
if(ASTIA_VGA_NANO_BUILD STREQUAL "")
    set(ASTIA_VGA_NANO_BUILD "${CMAKE_BINARY_DIR}/nano" CACHE PATH
        "Nano build directory for the Astia VGA launcher" FORCE)
endif()
set(ASTIA_VGA_RUN "$ENV{ASTIA_ZSH_VGA_RUN}" CACHE PATH
    "Runtime artifact directory for the Astia VGA launcher")
if(ASTIA_VGA_RUN STREQUAL "")
    set(ASTIA_VGA_RUN "${CMAKE_BINARY_DIR}/vga-run" CACHE PATH
        "Runtime artifact directory for the Astia VGA launcher" FORCE)
endif()
set(ASTIA_VGA_DISPLAY "gtk" CACHE STRING
    "QEMU display backend for the Astia VGA launcher")
set(ASTIA_VGA_FALLBACK "fail" CACHE STRING
    "VGA launcher fallback mode: fail or serial")
set(ASTIA_VGA_STRICT OFF CACHE BOOL
    "Require strict framebuffer checks in the Astia VGA launcher")
set(ASTIA_VGA_HARNESS OFF CACHE BOOL
    "Run the Astia VGA launcher smoke harness instead of interactive mode")

set(_astia_host_sanitizer_flags)
foreach(_astia_sanitizer_flag_source
        CMAKE_C_FLAGS
        CMAKE_CXX_FLAGS
        CMAKE_EXE_LINKER_FLAGS
        CMAKE_SHARED_LINKER_FLAGS
        CMAKE_MODULE_LINKER_FLAGS)
    string(REGEX MATCHALL "-fsanitize=[^ \t;]+" _astia_sanitizer_flags
            "${${_astia_sanitizer_flag_source}}")
    list(APPEND _astia_host_sanitizer_flags ${_astia_sanitizer_flags})
    string(REGEX REPLACE "(^|[ \t])-fsanitize=[^ \t;]+" ""
            _astia_sanitizer_cleaned "${${_astia_sanitizer_flag_source}}")
    set(${_astia_sanitizer_flag_source} "${_astia_sanitizer_cleaned}")
endforeach()
list(REMOVE_DUPLICATES _astia_host_sanitizer_flags)
set(ASTIA_HOST_SANITIZER_COMPILE_OPTIONS
        ${_astia_host_sanitizer_flags})
set(ASTIA_HOST_SANITIZER_LINK_OPTIONS
        ${_astia_host_sanitizer_flags})

if(ASTIA_VGA_NANO)
    set(_astia_vga_nano 1)
else()
    set(_astia_vga_nano 0)
endif()
if(ASTIA_VGA_STRICT)
    set(_astia_vga_strict 1)
else()
    set(_astia_vga_strict 0)
endif()
if(ASTIA_VGA_HARNESS)
    set(_astia_vga_harness 1)
else()
    set(_astia_vga_harness 0)
endif()

add_custom_target(astia_vga_qemu
    COMMAND ${CMAKE_COMMAND} -E env
        "QEMU=${ASTIA_VGA_QEMU}"
        "ASTIA_VGA_MEMORY=${ASTIA_VGA_MEMORY}"
        "ASTIA_VGA_CPUS=${ASTIA_VGA_CPUS}"
        "ASTIA_NANO=${_astia_vga_nano}"
        "ASTIA_NANO_BUILD=${ASTIA_VGA_NANO_BUILD}"
        "ZSH_BUILD=${ASTIA_VGA_ZSH_BUILD}"
        "ASTIA_KERNEL_BUILD=${ASTIA_VGA_KERNEL_BUILD}"
        "ASTIA_ZSH_VGA_RUN=${ASTIA_VGA_RUN}"
        "ASTIA_VGA_DISPLAY=${ASTIA_VGA_DISPLAY}"
        "ASTIA_VGA_FALLBACK=${ASTIA_VGA_FALLBACK}"
        "ASTIA_VGA_STRICT=${_astia_vga_strict}"
        "ASTIA_ZSH_VGA_HARNESS=${_astia_vga_harness}"
        "${CMAKE_SOURCE_DIR}/toolchain/run-zsh-vga-qemu.sh"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    USES_TERMINAL)

set(ASTIA_DIRECT_QEMU "$ENV{QEMU}" CACHE STRING
    "QEMU executable used by the direct VGA/GDB launcher")
if(ASTIA_DIRECT_QEMU STREQUAL "")
    set(ASTIA_DIRECT_QEMU "qemu-system-x86_64" CACHE STRING
        "QEMU executable used by the direct VGA/GDB launcher" FORCE)
endif()
set(ASTIA_DIRECT_MEMORY "256M" CACHE STRING
    "QEMU memory size for the direct VGA/GDB launcher")
set(ASTIA_DIRECT_CPUS "2" CACHE STRING
    "QEMU virtual CPU count for the direct VGA/GDB launcher")
set(ASTIA_DIRECT_GDB_PORT "1234" CACHE STRING
    "GDB remote port for the direct VGA/GDB launcher")
set(ASTIA_DIRECT_DISPLAY "gtk" CACHE STRING
    "QEMU display backend for the direct VGA/GDB launcher")
set(ASTIA_DIRECT_GDB_MODE "wait" CACHE STRING
    "Direct VGA/GDB startup mode: wait or attach")
set_property(CACHE ASTIA_DIRECT_GDB_MODE PROPERTY STRINGS wait attach)
set(ASTIA_DIRECT_BUILD "${CMAKE_BINARY_DIR}/direct-kernel" CACHE PATH
    "Kernel build directory for the direct VGA/GDB launcher")
set(ASTIA_DIRECT_INITRD "${CMAKE_BINARY_DIR}/direct-vga-gdb-initrd.cpio" CACHE FILEPATH
    "Initrd artifact for the direct VGA/GDB launcher")

add_custom_target(astia_direct_vga_gdb
    COMMAND ${CMAKE_COMMAND} -E env
        "QEMU=${ASTIA_DIRECT_QEMU}"
        "ASTIA_DIRECT_MEMORY=${ASTIA_DIRECT_MEMORY}"
        "ASTIA_DIRECT_CPUS=${ASTIA_DIRECT_CPUS}"
        "ASTIA_DIRECT_GDB_PORT=${ASTIA_DIRECT_GDB_PORT}"
        "ASTIA_DIRECT_DISPLAY=${ASTIA_DIRECT_DISPLAY}"
        "ASTIA_DIRECT_GDB_MODE=${ASTIA_DIRECT_GDB_MODE}"
        "ASTIA_DIRECT_BUILD=${ASTIA_DIRECT_BUILD}"
        "ASTIA_DIRECT_INITRD=${ASTIA_DIRECT_INITRD}"
        "${CMAKE_SOURCE_DIR}/kernel/scripts/run-direct-vga-gdb.sh"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    USES_TERMINAL)

add_subdirectory(kernel)
if(ASTIA_BUILD_TESTS)
    add_subdirectory(libc)
endif()
add_subdirectory(modules EXCLUDE_FROM_ALL)

if(ASTIA_BUILD_TESTS)
    foreach(_astia_module_test_target
            astia_typed_contract_layout_target
            astia_typed_module
            astia_typed_header_alone
            virtio_blk_unit_target
            usb_unit_target
            rtl8139_unit_target)
        if(NOT TARGET ${_astia_module_test_target})
            message(FATAL_ERROR
                    "ASTIA_BUILD_TESTS requires module target ${_astia_module_test_target}")
        endif()
    endforeach()

    add_custom_target(astia_module_host_tests ALL)
    add_dependencies(astia_module_host_tests
            astia_typed_contract_layout_target
            astia_typed_module
            astia_typed_header_alone
            virtio_blk_unit_target
            usb_unit_target
            rtl8139_unit_target)

    add_test(NAME astia_module_host_targets
            COMMAND ${CMAKE_COMMAND}
            -DASTIA_TYPED_CONTRACT_LAYOUT=${CMAKE_CURRENT_BINARY_DIR}/modules/tests/typed_contract_layout
            -DASTIA_VIRTIO_BLK_UNIT=${CMAKE_CURRENT_BINARY_DIR}/modules/virtio-blk/virtio_blk_unit
            -DASTIA_USB_UNIT=${CMAKE_CURRENT_BINARY_DIR}/modules/usb/usb_unit
            -DASTIA_RTL8139_UNIT=${CMAKE_CURRENT_BINARY_DIR}/modules/rtl8139/rtl8139_unit
            -P ${CMAKE_CURRENT_SOURCE_DIR}/modules/tests/host_tests_regression.cmake)
    set_tests_properties(astia_module_host_targets
            PROPERTIES LABELS "module;abi;host;deterministic" TIMEOUT 60)

    set(_astia_kernel_runtime ${CMAKE_BINARY_DIR}/kernel/src/astiakrn_runtime)
    set(_astia_boot_initrd ${CMAKE_BINARY_DIR}/kernel/tests/fixtures/boot_initrd.cpio)
    set(_astia_cross_build ${CMAKE_BINARY_DIR}/kernel/tests/fixtures/clang)
    set(_astia_sysroot ${_astia_cross_build}/sysroot)
    set(_astia_crt0 ${_astia_cross_build}/image/crt0.o)
    set(_astia_qemu qemu-system-x86_64)
    if(DEFINED ENV{QEMU} AND NOT "$ENV{QEMU}" STREQUAL "")
        set(_astia_qemu "$ENV{QEMU}")
    endif()

    astia_register_script_test(astia_libc_cross
            "libc;cross;deterministic"
            ${CMAKE_CURRENT_SOURCE_DIR}/toolchain/build-init-libc.sh
            TIMEOUT 300
            ENV
                ASTIA_CROSS_BUILD=${CMAKE_BINARY_DIR}/cross-libc
                ASTIA_SYSROOT=${CMAKE_BINARY_DIR}/cross-libc/sysroot)

    astia_register_script_test(astia_coreutils_qemu
            "coreutils;qemu;deterministic"
            ${CMAKE_CURRENT_SOURCE_DIR}/coreutils/tests/run.sh
            TIMEOUT 120
            ENV
                QEMU=${_astia_qemu}
                ASTIA_KERNEL=${_astia_kernel_runtime}
                ASTIA_INITRD=${_astia_boot_initrd}
                ASTIA_SYSROOT=${_astia_sysroot}
                ASTIA_CRT0=${_astia_crt0}
                ASTIA_INIT_LIBC=${_astia_cross_build}/image/init_libc
                COREUTILS_TEST_BUILD=${CMAKE_BINARY_DIR}/coreutils-test)
    set_tests_properties(astia_coreutils_qemu PROPERTIES
            DEPENDS astia_libc_cross RESOURCE_LOCK astia-qemu RUN_SERIAL TRUE)

    astia_register_script_test(astia_nvme_msix
            "nvme;qemu;msix;read-write;multi-namespace;deterministic"
            ${CMAKE_CURRENT_SOURCE_DIR}/modules/nvme/run-qemu.sh
            TIMEOUT 120
            ENV
                QEMU=${_astia_qemu}
                NVME_BUILD=${CMAKE_BINARY_DIR}/nvme-msix
                NVME_CASE=msix)
    astia_register_script_test(astia_nvme_polling
            "nvme;qemu;polling;read-write;multi-namespace;deterministic"
            ${CMAKE_CURRENT_SOURCE_DIR}/modules/nvme/run-qemu.sh
            TIMEOUT 120
            ENV
                QEMU=${_astia_qemu}
                NVME_BUILD=${CMAKE_BINARY_DIR}/nvme-polling
                NVME_CASE=polling)
    set_tests_properties(astia_nvme_msix astia_nvme_polling
            PROPERTIES RESOURCE_LOCK astia-qemu RUN_SERIAL TRUE)

    astia_register_script_test(astia_virtio_blk_read_write
            "virtio;qemu;read-write;deterministic"
            ${CMAKE_CURRENT_SOURCE_DIR}/modules/virtio-blk/run-qemu.sh
            TIMEOUT 120
            ENV
                QEMU=${_astia_qemu}
                VIRTIO_BLK_BUILD=${CMAKE_BINARY_DIR}/virtio-read-write
                VIRTIO_BLK_READONLY=0)
    astia_register_script_test(astia_virtio_blk_read_only
            "virtio;qemu;read-only;deterministic"
            ${CMAKE_CURRENT_SOURCE_DIR}/modules/virtio-blk/run-qemu.sh
            TIMEOUT 120
            ENV
                QEMU=${_astia_qemu}
                VIRTIO_BLK_BUILD=${CMAKE_BINARY_DIR}/virtio-read-only
                VIRTIO_BLK_READONLY=1)
    set_tests_properties(astia_virtio_blk_read_write astia_virtio_blk_read_only
            PROPERTIES RESOURCE_LOCK astia-qemu RUN_SERIAL TRUE)

    astia_register_script_test(astia_usb_xhci_qemu
            "usb;xhci;qemu;deterministic"
            ${CMAKE_CURRENT_SOURCE_DIR}/modules/usb/run-qemu.sh
            TIMEOUT 30
            ENV QEMU=${_astia_qemu})
    set_tests_properties(astia_usb_xhci_qemu PROPERTIES
            RESOURCE_LOCK astia-qemu RUN_SERIAL TRUE SKIP_RETURN_CODE 77)

    set(_astia_zsh_env)
    if(ASTIA_ZSH_SOURCE)
        list(APPEND _astia_zsh_env ZSH_SOURCE=${ASTIA_ZSH_SOURCE})
    endif()
    astia_register_script_test(astia_zsh_pty_regression
            "zsh;pty;qemu;external;deterministic"
            ${CMAKE_CURRENT_SOURCE_DIR}/toolchain/run-zsh-qemu.sh
            EXTERNAL TIMEOUT 300
            ENV
                ${_astia_zsh_env}
                QEMU=${_astia_qemu}
                ZSH_BUILD=${CMAKE_BINARY_DIR}/external/zsh
                ASTIA_KERNEL=${_astia_kernel_runtime}
                ASTIA_INITRD=${_astia_boot_initrd}
                ASTIA_ZSH_RUN=${CMAKE_BINARY_DIR}/external/zsh-pty
                ASTIA_ZSH_HARNESS=1)
    astia_register_script_test(astia_zsh_signal_regression
            "zsh;qemu;external;deterministic"
            ${CMAKE_CURRENT_SOURCE_DIR}/toolchain/run-zsh-regression.sh
            EXTERNAL TIMEOUT 180
            ENV
                ${_astia_zsh_env}
                QEMU=${_astia_qemu}
                ZSH_BUILD=${CMAKE_BINARY_DIR}/external/zsh
                ASTIA_KERNEL=${_astia_kernel_runtime}
                ASTIA_INITRD=${_astia_boot_initrd}
                ASTIA_ZSH_REGRESSION=${CMAKE_BINARY_DIR}/external/zsh-regression)
    astia_register_script_test(astia_zsh_interactive_manual
            "zsh;pty;qemu;manual"
            ${CMAKE_CURRENT_SOURCE_DIR}/toolchain/run-zsh-qemu.sh
            MANUAL TIMEOUT 300
            ENV
                ${_astia_zsh_env}
                ASTIA_MANUAL=1
                QEMU=${_astia_qemu}
                ZSH_BUILD=${CMAKE_BINARY_DIR}/external/zsh
                ASTIA_KERNEL=${_astia_kernel_runtime}
                ASTIA_INITRD=${_astia_boot_initrd}
                ASTIA_ZSH_RUN=${CMAKE_BINARY_DIR}/external/zsh-manual)

    astia_register_script_test(astia_nano_pty_smoke
            "nano;pty;qemu;external;deterministic"
            ${CMAKE_CURRENT_SOURCE_DIR}/toolchain/run-nano-qemu-smoke.sh
            EXTERNAL TIMEOUT 120
            ENV
                QEMU=${_astia_qemu}
                ASTIA_KERNEL=${_astia_kernel_runtime}
                ASTIA_INITRD=${_astia_boot_initrd}
                ASTIA_SYSROOT=${_astia_sysroot}
                ASTIA_CRT0=${_astia_crt0}
                NANO_SMOKE_BUILD=${CMAKE_BINARY_DIR}/external/nano-smoke)

    set(_astia_vga_env)
    if(ASTIA_ZSH_SOURCE)
        list(APPEND _astia_vga_env ZSH_SOURCE=${ASTIA_ZSH_SOURCE})
    endif()
    astia_register_script_test(astia_vga_graphical
            "vga;graphical;qemu;ttv0;external;manual"
            ${CMAKE_CURRENT_SOURCE_DIR}/toolchain/run-zsh-vga-qemu.sh
            EXTERNAL MANUAL TIMEOUT 360
            ENV
                ${_astia_vga_env}
                ASTIA_MANUAL=1
                QEMU=${_astia_qemu}
                ZSH_BUILD=${CMAKE_BINARY_DIR}/external/zsh
                ASTIA_KERNEL_BUILD=${CMAKE_BINARY_DIR}/kernel
                ASTIA_ZSH_VGA_RUN=${CMAKE_BINARY_DIR}/external/vga
                ASTIA_ZSH_VGA_HARNESS=1
                ASTIA_VGA_FALLBACK=fail)
    astia_register_script_test(astia_vga_pvh_fallback
            "vga;pvh;uart;qemu;ttv0;external;deterministic"
            ${CMAKE_CURRENT_SOURCE_DIR}/toolchain/run-zsh-vga-qemu.sh
            EXTERNAL TIMEOUT 300
            ENV
                ${_astia_vga_env}
                QEMU=${_astia_qemu}
                ZSH_BUILD=${CMAKE_BINARY_DIR}/external/zsh
                ASTIA_KERNEL_BUILD=${CMAKE_BINARY_DIR}/kernel
                ASTIA_ZSH_VGA_RUN=${CMAKE_BINARY_DIR}/external/vga-pvh
                ASTIA_ZSH_VGA_HARNESS=1
                ASTIA_VGA_FALLBACK=serial
                ASTIA_VGA_DISPLAY=none)

    set(_astia_posix_env)
    if(ASTIA_POSIX_TEST_SOURCE)
        list(APPEND _astia_posix_env POSIX_TEST_SOURCE=${ASTIA_POSIX_TEST_SOURCE})
    endif()
    astia_register_script_test(astia_posix_batch
            "posix;ltp;qemu;external;deterministic"
            ${CMAKE_CURRENT_SOURCE_DIR}/toolchain/run-posix-tests.sh
            EXTERNAL TIMEOUT 900
            ENV
                ${_astia_posix_env}
                QEMU=${_astia_qemu}
                ASTIA_KERNEL=${_astia_kernel_runtime}
                ASTIA_INITRD=${_astia_boot_initrd}
                ASTIA_CROSS_BUILD=${CMAKE_BINARY_DIR}/cross-libc
                ASTIA_SYSROOT=${CMAKE_BINARY_DIR}/cross-libc/sysroot
                ASTIA_CRT0=${CMAKE_BINARY_DIR}/cross-libc/image/crt0.o
                POSIX_TEST_BUILD=${CMAKE_BINARY_DIR}/external/posix)
    set_tests_properties(astia_posix_batch PROPERTIES
            DEPENDS astia_libc_cross RESOURCE_LOCK astia-qemu RUN_SERIAL TRUE)

    astia_append_test_label("repository")
endif()
