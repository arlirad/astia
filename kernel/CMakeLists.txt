cmake_minimum_required(VERSION 3.20)

project(astiakernel
        VERSION 0.1.0
        DESCRIPTION "Modular kernel (C++20, no exceptions)"
        LANGUAGES C CXX ASM)

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
