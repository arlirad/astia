cmake_minimum_required(VERSION 3.20)

project(astiakernel
        VERSION 0.1.0
        DESCRIPTION "Modular kernel (C++20, no exceptions)"
        LANGUAGES C CXX ASM)

# Keep paths rooted at the kernel when it is included by the repository-level
# IDE project.  Direct `cmake -S kernel` builds get the same value.
set(ASTIA_KERNEL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL
        "Astia kernel source directory")

# Global settings
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# -mgeneral-regs-only: the kernel never uses SSE even though it is
# enabled for userspace (init_gdt_tss sets CR4.OSFXSR so ring-3 code
# may); kernel XMM codegen would have no state management behind it.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -fno-rtti -mgeneral-regs-only -Wall -Wextra -pedantic")

# Host-side unit tests (built with the system toolchain, run via CTest).
option(ASTIA_BUILD_TESTS "Build and register host-side unit tests" OFF)
option(ASTIA_BUILD_EXPERIMENTAL_FIBER
        "Compile the unintegrated stackful core::fiber implementation" OFF)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif ()

# Architecture selection
set(ARCH "x86_64" CACHE STRING "Target architecture: x86_64, aarch64")
set_property(CACHE ARCH PROPERTY STRINGS x86_64 aarch64)

# Platform/Boot selection (can be overridden)
set(PLATFORM "virt" CACHE STRING "Target platform: virt, pc")
set_property(CACHE PLATFORM PROPERTY STRINGS virt pc)
set(BOOT "virt" CACHE STRING "Target boot: virt, pc")
set_property(CACHE BOOT PROPERTY STRINGS virt pc)

if (ARCH STREQUAL "aarch64")
    message(FATAL_ERROR
            "Unsupported AArch64 kernel target (ARCH=${ARCH}, PLATFORM=${PLATFORM}, BOOT=${BOOT}): "
            "the AArch64 port is bring-up only and has no complete boot, linker, or cross-toolchain target")
endif ()

if (NOT ARCH STREQUAL "x86_64" OR NOT PLATFORM STREQUAL "virt" OR NOT BOOT STREQUAL "virt")
    message(FATAL_ERROR
            "Unsupported kernel target (ARCH=${ARCH}, PLATFORM=${PLATFORM}, BOOT=${BOOT}); "
            "the supported target is ARCH=x86_64, PLATFORM=virt, BOOT=virt")
endif ()

set(ASTIA_LINKER_SCRIPT
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/linker/${BOOT}-${ARCH}.ld"
        CACHE INTERNAL "Linker script selected by the kernel target")
if (NOT EXISTS "${ASTIA_LINKER_SCRIPT}")
    message(FATAL_ERROR
            "No linker script for kernel target (ARCH=${ARCH}, PLATFORM=${PLATFORM}, BOOT=${BOOT}): "
            "${ASTIA_LINKER_SCRIPT}")
endif ()

# Generate compile_commands.json for tooling
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler-specific settings
include(cmake/Compilers.cmake)

# Include module CMake files
add_subdirectory(src)

# Host-side unit tests (opt-in).
if (ASTIA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()
