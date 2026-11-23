# Standalone cross toolchain for the QEMU virt AArch64 smoke image.
#
# This configuration is intentionally independent of the production kernel
# and x86 toolchains.  It only supplies a freestanding AArch64 compiler; the
# image supplies its own entry point, linker script and runtime.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(ASTIA_AARCH64_VIRT_CXX_COMPILER "" CACHE FILEPATH
        "Cross C++ compiler for the standalone AArch64 virt smoke image")
if (NOT ASTIA_AARCH64_VIRT_CXX_COMPILER)
    find_program(_astia_aarch64_virt_cxx_compiler
            NAMES aarch64-linux-gnu-g++ aarch64-none-elf-g++
            DOC "Cross C++ compiler for the standalone AArch64 virt smoke image")
    if (NOT _astia_aarch64_virt_cxx_compiler)
        message(FATAL_ERROR
                "An AArch64 C++ compiler is required; install "
                "g++-aarch64-linux-gnu or set ASTIA_AARCH64_VIRT_CXX_COMPILER")
    endif ()
    set(ASTIA_AARCH64_VIRT_CXX_COMPILER
            "${_astia_aarch64_virt_cxx_compiler}"
            CACHE FILEPATH
            "Cross C++ compiler for the standalone AArch64 virt smoke image"
            FORCE)
endif ()

set(CMAKE_CXX_COMPILER "${ASTIA_AARCH64_VIRT_CXX_COMPILER}" CACHE FILEPATH "" FORCE)
set(CMAKE_ASM_COMPILER "${ASTIA_AARCH64_VIRT_CXX_COMPILER}" CACHE FILEPATH "" FORCE)

set(CMAKE_CXX_FLAGS_INIT "-ffreestanding -fno-exceptions -fno-rtti -fno-builtin -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-threadsafe-statics -fno-use-cxa-atexit -mgeneral-regs-only -mno-outline-atomics")
set(CMAKE_ASM_FLAGS_INIT "-ffreestanding -fno-pic -fno-pie")
set(CMAKE_EXE_LINKER_FLAGS_INIT
        "-nostdlib -nostartfiles -nodefaultlibs -static -no-pie")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
