# Freestanding AArch64 cross toolchain for the QEMU virt kernel target.
#
# Select it explicitly on the kernel configure line:
#   -DCMAKE_TOOLCHAIN_FILE=kernel/cmake/toolchain-aarch64-virt.cmake
# The x86 target keeps using the host/default toolchain.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(ASTIA_AARCH64_VIRT_C_COMPILER "" CACHE FILEPATH
        "Cross C compiler for the AArch64 virt kernel target")
if (NOT ASTIA_AARCH64_VIRT_C_COMPILER)
    find_program(_astia_aarch64_virt_c_compiler
            NAMES aarch64-linux-gnu-gcc aarch64-none-elf-gcc
            DOC "Cross C compiler for the AArch64 virt kernel target")
    if (NOT _astia_aarch64_virt_c_compiler)
        message(FATAL_ERROR
                "An AArch64 C compiler is required; install gcc-aarch64-linux-gnu "
                "or set ASTIA_AARCH64_VIRT_C_COMPILER")
    endif ()
    set(ASTIA_AARCH64_VIRT_C_COMPILER
            "${_astia_aarch64_virt_c_compiler}"
            CACHE FILEPATH
            "Cross C compiler for the AArch64 virt kernel target"
            FORCE)
endif ()

set(ASTIA_AARCH64_VIRT_CXX_COMPILER "" CACHE FILEPATH
        "Cross C++ compiler for the AArch64 virt kernel target")
if (NOT ASTIA_AARCH64_VIRT_CXX_COMPILER)
    find_program(_astia_aarch64_virt_cxx_compiler
            NAMES aarch64-linux-gnu-g++ aarch64-none-elf-g++
            DOC "Cross C++ compiler for the AArch64 virt kernel target")
    if (NOT _astia_aarch64_virt_cxx_compiler)
        message(FATAL_ERROR
                "An AArch64 C++ compiler is required; install g++-aarch64-linux-gnu "
                "or set ASTIA_AARCH64_VIRT_CXX_COMPILER")
    endif ()
    set(ASTIA_AARCH64_VIRT_CXX_COMPILER
            "${_astia_aarch64_virt_cxx_compiler}"
            CACHE FILEPATH
            "Cross C++ compiler for the AArch64 virt kernel target"
            FORCE)
endif ()

set(CMAKE_C_COMPILER "${ASTIA_AARCH64_VIRT_C_COMPILER}" CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER "${ASTIA_AARCH64_VIRT_CXX_COMPILER}" CACHE FILEPATH "" FORCE)
set(CMAKE_ASM_COMPILER "${ASTIA_AARCH64_VIRT_C_COMPILER}" CACHE FILEPATH "" FORCE)

set(CMAKE_C_FLAGS_INIT "-ffreestanding -fno-builtin -fno-pie -fno-pic -fno-stack-protector -mno-outline-atomics")
set(CMAKE_CXX_FLAGS_INIT "-ffreestanding -fno-builtin -fno-pie -fno-pic -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-threadsafe-statics -fno-use-cxa-atexit -mno-outline-atomics")
set(CMAKE_ASM_FLAGS_INIT "-ffreestanding -fno-pie -fno-pic")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
