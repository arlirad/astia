# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Arlirad

cmake_minimum_required(VERSION 3.20)

# The repository-level project is the canonical entry point for IDEs.  The
# kernel remains independently configurable from kernel/, while libc keeps
# its separate freestanding toolchain and install/sysroot workflow.
project(astia LANGUAGES NONE)

enable_testing()

add_test(NAME astia_license_metadata
        COMMAND ${CMAKE_COMMAND} -P
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/license_metadata_check.cmake)
add_test(NAME astia_vga_launcher_defaults
        COMMAND ${CMAKE_COMMAND} -P
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/vga_launcher_defaults_check.cmake)

option(ASTIA_BUILD_TESTS "Build and register host-side unit tests" OFF)

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

add_subdirectory(kernel)
add_subdirectory(modules EXCLUDE_FROM_ALL)

if(ASTIA_BUILD_TESTS)
    foreach(_astia_module_test_target
            astia_module_abi_layout_target
            astia_module_c_compatibility
            astia_module_cpp_compatibility
            astia_module_abi_header_alone
            virtio_blk_unit_target
            usb_unit_target)
        if(NOT TARGET ${_astia_module_test_target})
            message(FATAL_ERROR
                    "ASTIA_BUILD_TESTS requires module target ${_astia_module_test_target}")
        endif()
    endforeach()

    add_custom_target(astia_module_host_tests ALL)
    add_dependencies(astia_module_host_tests
            astia_module_abi_layout_target
            astia_module_c_compatibility
            astia_module_cpp_compatibility
            astia_module_abi_header_alone
            virtio_blk_unit_target
            usb_unit_target)

    add_test(NAME astia_module_host_targets
            COMMAND ${CMAKE_COMMAND}
            -DASTIA_MODULE_ABI_LAYOUT=${CMAKE_CURRENT_BINARY_DIR}/modules/tests/module_abi_layout
            -DASTIA_VIRTIO_BLK_UNIT=${CMAKE_CURRENT_BINARY_DIR}/modules/virtio-blk/virtio_blk_unit
            -DASTIA_USB_UNIT=${CMAKE_CURRENT_BINARY_DIR}/modules/usb/usb_unit
            -P ${CMAKE_CURRENT_SOURCE_DIR}/modules/tests/host_tests_regression.cmake)
endif()
