cmake_minimum_required(VERSION 3.20)

# ld.astia.so: the userspace ELF interpreter.  Built freestanding: it cannot
# depend on the libc it is meant to load.
project(astia_dl LANGUAGES C ASM)

# The entry stub and relocation encoding are per-architecture; the ELF and
# dynamic-section core is shared.
set(ASTIA_DL_ARCH "x86_64" CACHE STRING "Interpreter architecture")

add_library(ld_astia SHARED
        ld.c
        mode.c
        symcount.c
        ${CMAKE_CURRENT_SOURCE_DIR}/arch/${ASTIA_DL_ARCH}/start.S
        ${CMAKE_CURRENT_SOURCE_DIR}/arch/${ASTIA_DL_ARCH}/resolve.S)

target_include_directories(ld_astia PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/arch/${ASTIA_DL_ARCH}
        ${CMAKE_CURRENT_SOURCE_DIR}/../kernel/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../libc/include)

target_compile_options(ld_astia PRIVATE
        -ffreestanding -fno-stack-protector -fno-plt -fno-builtin
        -fno-asynchronous-unwind-tables -mno-red-zone
        -Wall -Wextra)

target_link_options(ld_astia PRIVATE
        -nostdlib
        -Wl,-e,ld_start
        -Wl,--hash-style=sysv
        -Wl,-Bsymbolic
        -Wl,-z,now)

set_target_properties(ld_astia PROPERTIES
        OUTPUT_NAME "ld.astia"
        PREFIX ""
        SUFFIX ".so")
