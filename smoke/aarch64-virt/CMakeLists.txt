# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Arlirad

cmake_minimum_required(VERSION 3.20)

project(astia_aarch64_virt_smoke LANGUAGES CXX ASM)

if (NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    message(FATAL_ERROR
            "This standalone smoke requires an AArch64 cross toolchain file")
endif ()

set(ASTIA_AARCH64_VIRT_LINKER_SCRIPT
        "${CMAKE_CURRENT_SOURCE_DIR}/linker/aarch64-virt.ld"
        CACHE FILEPATH
        "Linker script for the standalone AArch64 virt smoke image")

add_executable(astia-aarch64-virt-smoke
        src/entry.S
        src/main.cpp
        src/pl011.cpp
        src/psci.cpp)

set_target_properties(astia-aarch64-virt-smoke PROPERTIES
        OUTPUT_NAME astia-aarch64-virt-smoke
        SUFFIX ".elf"
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
        LINK_DEPENDS "${ASTIA_AARCH64_VIRT_LINKER_SCRIPT}")

target_include_directories(astia-aarch64-virt-smoke PRIVATE src)

target_compile_options(astia-aarch64-virt-smoke PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-ffunction-sections>
        $<$<COMPILE_LANGUAGE:CXX>:-fdata-sections>
        $<$<COMPILE_LANGUAGE:CXX>:-Wall>
        $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
        $<$<COMPILE_LANGUAGE:CXX>:-Werror>)

target_link_options(astia-aarch64-virt-smoke PRIVATE
        "-T${ASTIA_AARCH64_VIRT_LINKER_SCRIPT}"
        "-Wl,--build-id=none"
        "-Wl,--gc-sections"
        "-Wl,-z,max-page-size=4096"
        "-nostdlib"
        "-nostartfiles"
        "-nodefaultlibs"
        "-static"
        "-no-pie")
