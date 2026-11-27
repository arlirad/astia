# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Arlirad

cmake_minimum_required(VERSION 3.20)

# The repository-level project is the canonical entry point for IDEs.  The
# kernel remains independently configurable from kernel/, while libc keeps
# its separate freestanding toolchain and install/sysroot workflow.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/AstiaVersion.cmake)

project(astia
        VERSION ${ASTIA_VERSION_major}.${ASTIA_VERSION_minor}.${ASTIA_VERSION_patch}
        LANGUAGES NONE)

enable_testing()

add_test(NAME astia_version_tests
        COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_astia_version.py)
set_tests_properties(astia_version_tests PROPERTIES
        LABELS "repository;deterministic" TIMEOUT 60)
add_test(NAME astia_banner_assets
        COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/images/test_banner_assets.py)
set_tests_properties(astia_banner_assets PROPERTIES
        LABELS "repository;deterministic" TIMEOUT 30)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/AstiaCTest.cmake)

add_test(NAME astia_license_metadata
        COMMAND ${CMAKE_COMMAND} -P
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/license_metadata_check.cmake)
add_test(NAME astia_vga_launcher_defaults
        COMMAND ${CMAKE_COMMAND} -P
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/vga_launcher_defaults_check.cmake)
add_test(NAME astia_userspace_workflow
        COMMAND ${CMAKE_COMMAND} -P
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/userspace_workflow_check.cmake)
add_test(NAME astia_direct_vga_gdb_smoke
        COMMAND ${CMAKE_COMMAND} -P
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/direct_vga_gdb_smoke.cmake)
set_tests_properties(astia_license_metadata astia_vga_launcher_defaults
        astia_userspace_workflow
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
if(ASTIA_ZSH_SOURCE STREQUAL "")
    set(ASTIA_ZSH_SOURCE "${CMAKE_SOURCE_DIR}/user/zsh" CACHE PATH
            "Clean upstream zsh source tree for external CTest suites" FORCE)
endif()
set(ASTIA_POSIX_TEST_SOURCE "$ENV{POSIX_TEST_SOURCE}" CACHE PATH
        "Clean LTP Open POSIX source tree for external CTest suites")
set(ASTIA_NCURSES_SOURCE "$ENV{NCURSES_SOURCE}" CACHE PATH
        "Pinned ncurses source tree for the cross package")
if(ASTIA_NCURSES_SOURCE STREQUAL "")
    set(ASTIA_NCURSES_SOURCE "${CMAKE_SOURCE_DIR}/user/ncurses" CACHE PATH
            "Pinned ncurses source tree for the cross package" FORCE)
endif()
set(ASTIA_NCURSES_BUILD "$ENV{NCURSES_BUILD}" CACHE PATH
        "Cross ncurses build and package directory")
if(ASTIA_NCURSES_BUILD STREQUAL "")
    set(ASTIA_NCURSES_BUILD "${CMAKE_BINARY_DIR}/ncurses" CACHE PATH
            "Cross ncurses build and package directory" FORCE)
endif()
set(ASTIA_OPENSSH_SOURCE "$ENV{OPENSSH_SOURCE}" CACHE PATH
        "Pinned OpenSSH portable source tree")
if(ASTIA_OPENSSH_SOURCE STREQUAL "")
    set(ASTIA_OPENSSH_SOURCE "${CMAKE_SOURCE_DIR}/user/openssh" CACHE PATH
            "Pinned OpenSSH portable source tree" FORCE)
endif()
set(ASTIA_OPENSSH_BUILD "$ENV{OPENSSH_BUILD}" CACHE PATH
        "Cross OpenSSH build and output directory")
if(ASTIA_OPENSSH_BUILD STREQUAL "")
    set(ASTIA_OPENSSH_BUILD "${CMAKE_BINARY_DIR}/openssh" CACHE PATH
            "Cross OpenSSH build and output directory" FORCE)
endif()
set(ASTIA_OPENSSH_SHELL "$ENV{OPENSSH_SHELL}" CACHE PATH
        "Guest shell binary used by the OpenSSH command tests")
if(ASTIA_OPENSSH_SHELL STREQUAL "")
    set(ASTIA_OPENSSH_SHELL "${CMAKE_BINARY_DIR}/zsh/output/zsh" CACHE PATH
            "Guest shell binary used by the OpenSSH command tests" FORCE)
endif()

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
set(ASTIA_VGA_ZSH_SOURCE "$ENV{ZSH_SOURCE}" CACHE PATH
        "Clean upstream zsh source tree for the Astia VGA launcher")
if(ASTIA_VGA_ZSH_SOURCE STREQUAL "")
    set(ASTIA_VGA_ZSH_SOURCE "${CMAKE_SOURCE_DIR}/user/zsh" CACHE PATH
            "Clean upstream zsh source tree for the Astia VGA launcher" FORCE)
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
        "ZSH_SOURCE=${ASTIA_VGA_ZSH_SOURCE}"
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

set(ASTIA_DEV_INITRD_RUN "${CMAKE_BINARY_DIR}/dev-initrd" CACHE PATH
    "Build directory containing the zsh/nano development initrd")
set(ASTIA_DEV_INITRD_ZSH_BUILD "${CMAKE_BINARY_DIR}/zsh" CACHE PATH
    "Reusable zsh and cross-libc build directory for dev_initrd")
set(ASTIA_DEV_INITRD_NANO_BUILD "${CMAKE_BINARY_DIR}/nano" CACHE PATH
    "Reusable nano build directory for dev_initrd")
set(ASTIA_DEV_INITRD_KERNEL_BUILD "${CMAKE_BINARY_DIR}/kernel" CACHE PATH
    "Existing kernel build directory supplying the runtime and base initrd")

get_filename_component(_astia_dev_initrd_run
    "${ASTIA_DEV_INITRD_RUN}" ABSOLUTE BASE_DIR "${CMAKE_BINARY_DIR}")
if(CMAKE_BINARY_DIR STREQUAL CMAKE_SOURCE_DIR)
    message(FATAL_ERROR "dev_initrd requires an out-of-source CMake build directory")
endif()
get_filename_component(_astia_dev_initrd_run_parent
    "${_astia_dev_initrd_run}" DIRECTORY)
if(NOT _astia_dev_initrd_run_parent STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
        "ASTIA_DEV_INITRD_RUN must be a dedicated directory directly under "
        "the CMake binary directory: ${CMAKE_BINARY_DIR}")
endif()
foreach(_astia_dev_initrd_protected_path
        "${CMAKE_BINARY_DIR}"
        "${CMAKE_SOURCE_DIR}"
        "${ASTIA_DEV_INITRD_KERNEL_BUILD}"
        "${ASTIA_DEV_INITRD_ZSH_BUILD}"
        "${ASTIA_DEV_INITRD_NANO_BUILD}"
        "${CMAKE_BINARY_DIR}/cross-libc"
        "${CMAKE_BINARY_DIR}/CMakeFiles"
        "${CMAKE_BINARY_DIR}/Testing")
    get_filename_component(_astia_dev_initrd_protected
        "${_astia_dev_initrd_protected_path}" ABSOLUTE
        BASE_DIR "${CMAKE_BINARY_DIR}")
    if(_astia_dev_initrd_run STREQUAL _astia_dev_initrd_protected)
        message(FATAL_ERROR
            "ASTIA_DEV_INITRD_RUN overlaps a protected build/source directory: "
            "${_astia_dev_initrd_run}")
    endif()
endforeach()
if(EXISTS "${_astia_dev_initrd_run}")
    get_filename_component(_astia_dev_initrd_run_real
        "${_astia_dev_initrd_run}" REALPATH)
    if(NOT _astia_dev_initrd_run_real STREQUAL _astia_dev_initrd_run)
        message(FATAL_ERROR
            "ASTIA_DEV_INITRD_RUN must not be a symlink: ${_astia_dev_initrd_run}")
    endif()
endif()

file(GLOB_RECURSE _astia_dev_initrd_sources CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/user/zsh/*"
    "${CMAKE_SOURCE_DIR}/user/nano/*"
    "${CMAKE_SOURCE_DIR}/user/gnulib/*"
    "${CMAKE_SOURCE_DIR}/coreutils/src/*"
    "${CMAKE_SOURCE_DIR}/sysutils/src/*"
    "${CMAKE_SOURCE_DIR}/libc/*"
    "${CMAKE_SOURCE_DIR}/toolchain/nano-astia/*")
list(FILTER _astia_dev_initrd_sources EXCLUDE REGEX "/\\.git(/|$)")

set(_astia_dev_initrd_archive "${_astia_dev_initrd_run}/initrd.cpio")
add_custom_command(
    OUTPUT "${_astia_dev_initrd_archive}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_astia_dev_initrd_run}"
    COMMAND "${CMAKE_SOURCE_DIR}/toolchain/init-userspace.sh"
    COMMAND ${CMAKE_COMMAND} -E env
        "ASTIA_ZSH_BUILD_ONLY=1"
        "ASTIA_NANO=1"
        "ASTIA_DEV_INITRD_BUILD_ROOT=${CMAKE_BINARY_DIR}"
        "ASTIA_DEV_INITRD_RUN=${_astia_dev_initrd_run}"
        "ASTIA_ZSH_RUN=${_astia_dev_initrd_run}"
        "ZSH_BUILD=${ASTIA_DEV_INITRD_ZSH_BUILD}"
        "ASTIA_NANO_BUILD=${ASTIA_DEV_INITRD_NANO_BUILD}"
        "ASTIA_TEST_BUILD=${ASTIA_DEV_INITRD_KERNEL_BUILD}"
        "${CMAKE_SOURCE_DIR}/toolchain/run-zsh-qemu.sh"
    DEPENDS
        "${CMAKE_SOURCE_DIR}/.gitmodules"
        "${CMAKE_SOURCE_DIR}/toolchain/init-userspace.sh"
        "${CMAKE_SOURCE_DIR}/toolchain/run-zsh-qemu.sh"
        "${CMAKE_SOURCE_DIR}/toolchain/build-zsh.sh"
        "${CMAKE_SOURCE_DIR}/toolchain/build-nano.sh"
        "${CMAKE_SOURCE_DIR}/toolchain/build-init-libc.sh"
        "${CMAKE_SOURCE_DIR}/toolchain/astia-x86_64-clang.cmake"
        "${CMAKE_SOURCE_DIR}/toolchain/zsh-runner.c"
        "${CMAKE_SOURCE_DIR}/toolchain/line-reader.c"
        "${CMAKE_SOURCE_DIR}/toolchain/line-reader-runner.c"
        "${CMAKE_SOURCE_DIR}/toolchain/pty-test.c"
        "${CMAKE_SOURCE_DIR}/kernel/tests/crt0.S"
        "${CMAKE_SOURCE_DIR}/kernel/tests/init_libc.c"
        "${CMAKE_BINARY_DIR}/kernel/tests/fixtures/boot_initrd.cpio"
        "${CMAKE_SOURCE_DIR}/coreutils/build.sh"
        "${CMAKE_SOURCE_DIR}/sysutils/build.sh"
        ${_astia_dev_initrd_sources}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    USES_TERMINAL
    VERBATIM)
add_custom_target(dev_initrd DEPENDS "${_astia_dev_initrd_archive}")

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
    astia_register_script_test(astia_production_init_launch_failure
            "boot;init;qemu;deterministic"
            ${CMAKE_CURRENT_SOURCE_DIR}/kernel/tests/integration/run_init_launch_failure.sh
            TIMEOUT 15
            ENV
                QEMU=${ASTIA_VGA_QEMU}
                ASTIA_KERNEL=${CMAKE_BINARY_DIR}/kernel/src/astiakrn)
endif()
add_subdirectory(modules EXCLUDE_FROM_ALL)
add_subdirectory(pkg)

if(TARGET astiakrn_runtime)
    add_dependencies(dev_initrd astiakrn_runtime)
endif()
if(TARGET astia_boot_initrd)
    add_dependencies(dev_initrd astia_boot_initrd)
endif()

if(ASTIA_BUILD_TESTS AND ASTIA_ENABLE_EXTERNAL_TESTS)
    add_test(NAME astia_dev_initrd_clean_build
        COMMAND ${CMAKE_COMMAND}
            "-DASTIA_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
            "-DASTIA_PARENT_BINARY_DIR=${CMAKE_BINARY_DIR}"
            "-DASTIA_CLEAN_TEST_BINARY_DIR=${CMAKE_BINARY_DIR}/dev-initrd-clean-test"
            "-DASTIA_GENERATOR=${CMAKE_GENERATOR}"
            -P "${CMAKE_SOURCE_DIR}/cmake/dev_initrd_clean_build_check.cmake")
    set_tests_properties(astia_dev_initrd_clean_build PROPERTIES
        LABELS "build;userspace;external;deterministic"
        TIMEOUT 1800)
endif()

if(ASTIA_BUILD_TESTS)
    add_custom_target(astia_qemu_test_artifacts)
    foreach(_astia_qemu_target
            astiakrn
            astiakrn_runtime
            astiakrn_module_runtime
            astiakrn_test
            astiakrn_acpi
            astiakrn_acpi_reject
            astia_acpi_grub_iso
            astia_boot_initrd
            astia_production_exit_initrd
            astia_cross_libc
            astia_native_boot_initrd
            astia_native_service_boot_initrd
            astia_native_failure_boot_initrd
            astia_grub_iso
            nvme_msix_module
            nvme_polling_module
            virtio_blk_module
            usb_module
            rtl8139_module
            ps2_keyboard_module
            ip_module
            eth_module
            astia_ps2_keyboard_iso)
        if(TARGET ${_astia_qemu_target})
            add_dependencies(astia_qemu_test_artifacts ${_astia_qemu_target})
        endif()
    endforeach()
    if(TARGET astia_boot_initrd AND TARGET astia_cross_libc)
        add_dependencies(astia_boot_initrd astia_cross_libc)
    endif()
    add_test(NAME astia_qemu_prepare
            COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
            --target astia_qemu_test_artifacts)
    set_tests_properties(astia_qemu_prepare PROPERTIES
            LABELS "build;qemu;deterministic"
            FIXTURES_SETUP astia-qemu-artifacts
            TIMEOUT 900)

    foreach(_astia_module_test_target
            astia_typed_contract_layout_target
            astia_typed_module
            astia_typed_header_alone
            astia_module_msix_test
            astia_module_lifecycle_test
            virtio_blk_unit_target
            virtio_blk_teardown_target
            nvme_teardown_target
            nvme_start_target
            usb_unit_target
            rtl8139_unit_target
            ps2_controller_unit_target
            ps2_scancode_unit_target
            ps2_keyboard_unit_target)
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
            astia_module_msix_test
            astia_module_lifecycle_test
            virtio_blk_unit_target
            virtio_blk_teardown_target
            nvme_teardown_target
            nvme_start_target
            usb_unit_target
            rtl8139_unit_target
            ps2_controller_unit_target
            ps2_scancode_unit_target
            ps2_keyboard_unit_target)

    add_test(NAME astia_module_host_prepare
            COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
            --target astia_module_host_tests)
    set_tests_properties(astia_module_host_prepare PROPERTIES
            LABELS "build;module;host;deterministic"
            FIXTURES_SETUP astia-module-host
            TIMEOUT 300)

    add_test(NAME astia_module_host_targets
            COMMAND ${CMAKE_COMMAND}
            -DASTIA_TYPED_CONTRACT_LAYOUT=${CMAKE_CURRENT_BINARY_DIR}/modules/tests/typed_contract_layout
            -DASTIA_VIRTIO_BLK_UNIT=${CMAKE_CURRENT_BINARY_DIR}/modules/virtio-blk/virtio_blk_unit
            -DASTIA_USB_UNIT=${CMAKE_CURRENT_BINARY_DIR}/modules/usb/usb_unit
            -DASTIA_RTL8139_UNIT=${CMAKE_CURRENT_BINARY_DIR}/modules/rtl8139/rtl8139_unit
            -P ${CMAKE_CURRENT_SOURCE_DIR}/modules/tests/host_tests_regression.cmake)
    set_tests_properties(astia_module_host_targets
            PROPERTIES LABELS "module;abi;host;deterministic"
            TIMEOUT 60 FIXTURES_REQUIRED astia-module-host)
    set_tests_properties(astia_qemu_prepare
            PROPERTIES DEPENDS astia_module_host_prepare)

    set(_astia_kernel_runtime ${CMAKE_BINARY_DIR}/kernel/src/astiakrn_runtime)
    set(_astia_boot_initrd ${CMAKE_BINARY_DIR}/kernel/tests/fixtures/boot_initrd.cpio)
    set(_astia_cross_build ${CMAKE_BINARY_DIR}/cross-libc)
    set(_astia_sysroot ${_astia_cross_build}/sysroot)
    set(_astia_crt0 ${_astia_cross_build}/image/crt0.o)
    set(_astia_qemu qemu-system-x86_64)
    if(DEFINED ENV{QEMU} AND NOT "$ENV{QEMU}" STREQUAL "")
        set(_astia_qemu "$ENV{QEMU}")
    endif()

    if(TARGET astia_cross_libc)
        add_test(NAME astia_libc_cross
                COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
                --target astia_cross_libc)
        set_tests_properties(astia_libc_cross PROPERTIES
                LABELS "libc;cross;deterministic"
                TIMEOUT 300)
    else()
        astia_register_script_test(astia_libc_cross
                "libc;cross;deterministic"
                ${CMAKE_CURRENT_SOURCE_DIR}/toolchain/build-init-libc.sh
                TIMEOUT 300
                ENV
                    ASTIA_CROSS_BUILD=${CMAKE_BINARY_DIR}/cross-libc
                    ASTIA_SYSROOT=${CMAKE_BINARY_DIR}/cross-libc/sysroot)
    endif()
    set_tests_properties(astia_qemu_prepare PROPERTIES DEPENDS astia_libc_cross)

    add_custom_target(astia_ncurses_cross
        COMMAND ${CMAKE_COMMAND} -E env
            "NCURSES_SOURCE=${ASTIA_NCURSES_SOURCE}"
            "NCURSES_BUILD=${ASTIA_NCURSES_BUILD}"
            "NCURSES_PACKAGE=${ASTIA_NCURSES_BUILD}/package"
            "ASTIA_CROSS_BUILD=${_astia_cross_build}"
            "ASTIA_SYSROOT=${_astia_sysroot}"
            "ASTIA_CRT0=${_astia_crt0}"
            "${CMAKE_SOURCE_DIR}/toolchain/build-ncurses.sh"
        DEPENDS
            "${CMAKE_SOURCE_DIR}/toolchain/build-ncurses.sh"
            "${CMAKE_SOURCE_DIR}/toolchain/build-init-libc.sh"
            "${CMAKE_SOURCE_DIR}/kernel/tests/crt0.S"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        USES_TERMINAL)
    add_test(NAME astia_ncurses_cross
            COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
            --target astia_ncurses_cross)
    set_tests_properties(astia_ncurses_cross PROPERTIES
            LABELS "ncurses;cross;deterministic"
            TIMEOUT 600)

    add_custom_target(astia_ncurses_abi
        COMMAND ${CMAKE_COMMAND} -E env
            "NCURSES_PACKAGE=${ASTIA_NCURSES_BUILD}/package"
            "ASTIA_SYSROOT=${_astia_sysroot}"
            "ASTIA_CRT0=${_astia_crt0}"
            "NCURSES_ABI_BUILD=${CMAKE_BINARY_DIR}/ncurses-abi"
            sh "${CMAKE_SOURCE_DIR}/toolchain/tests/run-ncurses-abi.sh"
        DEPENDS
            "${CMAKE_SOURCE_DIR}/toolchain/tests/run-ncurses-abi.sh"
            "${CMAKE_SOURCE_DIR}/toolchain/tests/ncurses_abi_probe.c"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        USES_TERMINAL)
    add_dependencies(astia_ncurses_abi astia_ncurses_cross)
    add_test(NAME astia_ncurses_abi
            COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
            --target astia_ncurses_abi)
    set_tests_properties(astia_ncurses_abi PROPERTIES
            LABELS "ncurses;abi;cross;deterministic"
            DEPENDS astia_ncurses_cross
            TIMEOUT 300)

    astia_register_script_test(astia_ncurses_qemu_smoke
            "ncurses;qemu;external;deterministic"
            ${CMAKE_CURRENT_SOURCE_DIR}/toolchain/tests/run-ncurses-qemu-smoke.sh
            EXTERNAL TIMEOUT 120
            ENV
                QEMU=${_astia_qemu}
                ASTIA_KERNEL=${_astia_kernel_runtime}
                ASTIA_INITRD=${_astia_boot_initrd}
                NCURSES_PACKAGE=${ASTIA_NCURSES_BUILD}/package
                ASTIA_SYSROOT=${_astia_sysroot}
                ASTIA_CRT0=${_astia_crt0}
                NCURSES_SMOKE_BUILD=${CMAKE_BINARY_DIR}/ncurses-smoke)
    set_tests_properties(astia_ncurses_qemu_smoke PROPERTIES
            DEPENDS astia_ncurses_cross)

    astia_register_script_test(astia_coreutils_qemu
            "coreutils;qemu;deterministic"
            ${CMAKE_CURRENT_SOURCE_DIR}/coreutils/tests/run.sh
            TIMEOUT 240
            ENV
                QEMU=${_astia_qemu}
                ASTIA_KERNEL=${_astia_kernel_runtime}
                ASTIA_INITRD=${_astia_boot_initrd}
                ASTIA_SYSROOT=${_astia_sysroot}
                ASTIA_CRT0=${_astia_crt0}
                ASTIA_INIT_LIBC=${_astia_cross_build}/image/init_libc
                COREUTILS_TEST_BUILD=${CMAKE_BINARY_DIR}/coreutils-test
                ASTIA_TEST_BUILD=${CMAKE_BINARY_DIR})
    set_tests_properties(astia_coreutils_qemu PROPERTIES
            DEPENDS astia_libc_cross RESOURCE_LOCK astia-qemu)

    astia_register_script_test(astia_daemond_stop
            "daemond;host;deterministic"
            ${CMAKE_CURRENT_SOURCE_DIR}/daemond/tests/run.sh
            TIMEOUT 120
            ENV
                DAEMOND_TEST_BUILD=${CMAKE_BINARY_DIR}/daemond-test)

    astia_register_script_test(astia_sysutils_qemu
            "sysutils;qemu;deterministic"
            ${CMAKE_CURRENT_SOURCE_DIR}/sysutils/tests/run.sh
            TIMEOUT 180
            ENV
                QEMU=${_astia_qemu}
                ASTIA_KERNEL=${_astia_kernel_runtime}
                ASTIA_INITRD=${_astia_boot_initrd}
                ASTIA_SYSROOT=${_astia_sysroot}
                ASTIA_CRT0=${_astia_crt0}
                SYSUTILS_TEST_BUILD=${CMAKE_BINARY_DIR}/sysutils-test
                ASTIA_TEST_BUILD=${CMAKE_BINARY_DIR})
    set_tests_properties(astia_sysutils_qemu PROPERTIES
            DEPENDS astia_libc_cross RESOURCE_LOCK astia-qemu)

    astia_register_script_test(astia_nvme_msix
            "nvme;qemu;msix;read-write;multi-namespace;deterministic"
            ${CMAKE_CURRENT_SOURCE_DIR}/modules/nvme/run-qemu.sh
            TIMEOUT 120
            ENV
                QEMU=${_astia_qemu}
                NVME_BUILD=${CMAKE_BINARY_DIR}/nvme-msix
                NVME_CASE=msix
                ASTIA_TEST_BUILD=${CMAKE_BINARY_DIR})
    astia_register_script_test(astia_nvme_polling
            "nvme;qemu;polling;read-write;multi-namespace;deterministic"
            ${CMAKE_CURRENT_SOURCE_DIR}/modules/nvme/run-qemu.sh
            TIMEOUT 120
            ENV
                QEMU=${_astia_qemu}
                NVME_BUILD=${CMAKE_BINARY_DIR}/nvme-polling
                NVME_CASE=polling
                ASTIA_TEST_BUILD=${CMAKE_BINARY_DIR})
    set_tests_properties(astia_nvme_msix astia_nvme_polling
            PROPERTIES RESOURCE_LOCK "astia-qemu;astia-build-tree")

    astia_register_script_test(astia_virtio_blk_read_write
            "virtio;qemu;read-write;deterministic"
            ${CMAKE_CURRENT_SOURCE_DIR}/modules/virtio-blk/run-qemu.sh
            TIMEOUT 120
            ENV
                QEMU=${_astia_qemu}
                VIRTIO_BLK_BUILD=${CMAKE_BINARY_DIR}/virtio-read-write
                VIRTIO_BLK_READONLY=0
                ASTIA_TEST_BUILD=${CMAKE_BINARY_DIR})
    astia_register_script_test(astia_virtio_blk_read_only
            "virtio;qemu;read-only;deterministic"
            ${CMAKE_CURRENT_SOURCE_DIR}/modules/virtio-blk/run-qemu.sh
            TIMEOUT 120
            ENV
                QEMU=${_astia_qemu}
                VIRTIO_BLK_BUILD=${CMAKE_BINARY_DIR}/virtio-read-only
                VIRTIO_BLK_READONLY=1
                ASTIA_TEST_BUILD=${CMAKE_BINARY_DIR})
    set_tests_properties(astia_virtio_blk_read_write astia_virtio_blk_read_only
            PROPERTIES RESOURCE_LOCK "astia-qemu;astia-build-tree")

    astia_register_script_test(astia_usb_xhci_qemu
            "usb;xhci;qemu;deterministic"
            ${CMAKE_CURRENT_SOURCE_DIR}/modules/usb/run-qemu.sh
            TIMEOUT 30
            ENV
                QEMU=${_astia_qemu}
                ASTIA_USB_BUILD=${CMAKE_BINARY_DIR}/usb-state
                ASTIA_TEST_BUILD=${CMAKE_BINARY_DIR})
    set_tests_properties(astia_usb_xhci_qemu PROPERTIES
            RESOURCE_LOCK "astia-qemu;astia-build-tree" SKIP_RETURN_CODE 77)

    set(_astia_zsh_env)
    list(APPEND _astia_zsh_env ZSH_SOURCE=${ASTIA_ZSH_SOURCE})
    astia_register_script_test(astia_zsh_zshenv_prompt
            "zsh;repository;deterministic"
            ${CMAKE_CURRENT_SOURCE_DIR}/toolchain/check-zsh-zshenv-prompt.sh
            TIMEOUT 30
            ENV
                ASTIA_ZSH_ZSHENV_CHECK_BUILD=${CMAKE_BINARY_DIR}/zshenv-prompt-check)
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
                ASTIA_ZSH_HARNESS=1
                ASTIA_TEST_BUILD=${CMAKE_BINARY_DIR})
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
                ASTIA_ZSH_REGRESSION=${CMAKE_BINARY_DIR}/external/zsh-regression
                ASTIA_TEST_BUILD=${CMAKE_BINARY_DIR})
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
                NANO_SMOKE_BUILD=${CMAKE_BINARY_DIR}/external/nano-smoke
                ASTIA_TEST_BUILD=${CMAKE_BINARY_DIR})

    astia_register_script_test(astia_openssh_port
            "openssh;port;repository;deterministic"
            ${CMAKE_CURRENT_SOURCE_DIR}/toolchain/openssh/tests/check-port.sh
            TIMEOUT 120)
    set(_astia_openssh_qemu_env
            QEMU=${_astia_qemu}
            OPENSSH_SOURCE=${ASTIA_OPENSSH_SOURCE}
            OPENSSH_BUILD=${ASTIA_OPENSSH_BUILD}
            ASTIA_SYSROOT=${_astia_sysroot}
            ASTIA_CRT0=${_astia_crt0}
            ASTIA_KERNEL=${_astia_kernel_runtime}
            ASTIA_INITRD=${_astia_boot_initrd}
            ASTIA_TEST_BUILD=${CMAKE_BINARY_DIR})
    if(NOT ASTIA_OPENSSH_SHELL STREQUAL "")
        list(APPEND _astia_openssh_qemu_env
                OPENSSH_SHELL_BINARY=${ASTIA_OPENSSH_SHELL})
    endif()
    astia_register_script_test(openssh_sshd_qemu
            "openssh;sshd;qemu;external;deterministic"
            ${CMAKE_CURRENT_SOURCE_DIR}/toolchain/openssh/run-qemu.sh
            EXTERNAL TIMEOUT 600
            ENV
                ${_astia_openssh_qemu_env}
                OPENSSH_QEMU_MODE=sshd
                OPENSSH_QEMU_RUN=${CMAKE_BINARY_DIR}/external/openssh-sshd)
    set_tests_properties(openssh_sshd_qemu PROPERTIES SKIP_RETURN_CODE 77)
    astia_register_script_test(openssh_ssh_loopback_qemu
            "openssh;ssh;loopback;qemu;external;deterministic"
            ${CMAKE_CURRENT_SOURCE_DIR}/toolchain/openssh/run-qemu.sh
            EXTERNAL TIMEOUT 600
            ENV
                ${_astia_openssh_qemu_env}
                OPENSSH_QEMU_MODE=loopback
                OPENSSH_QEMU_RUN=${CMAKE_BINARY_DIR}/external/openssh-loopback)
    set_tests_properties(openssh_ssh_loopback_qemu
            PROPERTIES SKIP_RETURN_CODE 77)

    set(_astia_vga_env)
    list(APPEND _astia_vga_env ZSH_SOURCE=${ASTIA_ZSH_SOURCE})
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
                ASTIA_VGA_FALLBACK=fail
                ASTIA_TEST_BUILD=${CMAKE_BINARY_DIR})
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
                ASTIA_VGA_DISPLAY=none
                ASTIA_TEST_BUILD=${CMAKE_BINARY_DIR})

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
                POSIX_TEST_BUILD=${CMAKE_BINARY_DIR}/external/posix
                ASTIA_TEST_BUILD=${CMAKE_BINARY_DIR})
    set_tests_properties(astia_posix_batch PROPERTIES
            DEPENDS astia_libc_cross RESOURCE_LOCK astia-qemu)

    set(_astia_shared_qemu_tests
            astia_coreutils_qemu
            astia_sysutils_qemu
            astia_nvme_msix
            astia_nvme_polling
            astia_virtio_blk_read_write
            astia_virtio_blk_read_only
            astia_usb_xhci_qemu
            astia_zsh_pty_regression
            astia_zsh_signal_regression
            astia_zsh_interactive_manual
            astia_nano_pty_smoke
            astia_ncurses_qemu_smoke
            openssh_sshd_qemu
            openssh_ssh_loopback_qemu
            astia_vga_graphical
            astia_vga_pvh_fallback
            astia_posix_batch
            astia_kernel_boot
            astia_fs_registry_boot
            astia_kernel_lifecycle_smp2
            astia_kernel_smp_boot
            astia_kernel_scheduler_smp2
            astia_initrd_boot
            astia_chroot
            astia_native_daemond_boot
            astia_native_daemond_service
            astia_native_daemond_failure
            astia_native_daemond_escalation
            astia_grub_smp_boot
            astia_grub_framebuffer_boot
            astia_grub_smp4_boot
            usb_xhci_qemu_enumeration
            rtl8139_qemu_module_load)
    foreach(_astia_shared_qemu_test IN LISTS _astia_shared_qemu_tests)
        if(TEST ${_astia_shared_qemu_test})
            set_tests_properties(${_astia_shared_qemu_test}
                    PROPERTIES FIXTURES_REQUIRED astia-qemu-artifacts)
        endif()
    endforeach()
    if(TEST usb_xhci_qemu_enumeration)
        set_tests_properties(usb_xhci_qemu_enumeration
                PROPERTIES ENVIRONMENT "ASTIA_TEST_BUILD=${CMAKE_BINARY_DIR}")
    endif()
    file(WRITE "${CMAKE_BINARY_DIR}/rtl8139-build-tree-lock.cmake"
            [=[set_tests_properties(rtl8139_qemu_module_load
                    PROPERTIES RESOURCE_LOCK "astia-build-tree")]=])
    set_property(DIRECTORY pkg APPEND PROPERTY TEST_INCLUDE_FILES
            "${CMAKE_BINARY_DIR}/rtl8139-build-tree-lock.cmake")

    # CTest build steps share one CMake binary tree; serialize them so a
    # parallel CTest run cannot invoke Ninja concurrently.
    set(_astia_build_tree_tests
            astia_libc_cross
            astia_ncurses_cross
            astia_ncurses_abi
            astia_qemu_prepare
            astia_module_host_prepare)
    foreach(_astia_build_test IN LISTS _astia_build_tree_tests)
        if(TEST ${_astia_build_test})
            set_tests_properties(${_astia_build_test}
                    PROPERTIES RESOURCE_LOCK astia-build-tree)
        endif()
    endforeach()

    astia_append_test_label("repository")
endif()
