# Clang cross toolchain for Astia userspace.
#
# This is intentionally a compiler/sysroot boundary, not a kernel toolchain:
# userspace must see Astia's headers and libc, while the compiler contributes
# only language builtins.  The target triple is a conventional ELF target so
# upstream configure scripts accept it without a custom config.sub patch.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_COMPILER clang CACHE FILEPATH "Clang C compiler")
set(CMAKE_ASM_COMPILER clang CACHE FILEPATH "Clang assembler driver")
set(CMAKE_C_COMPILER_TARGET x86_64-unknown-elf)
set(CMAKE_ASM_COMPILER_TARGET x86_64-unknown-elf)
set(CMAKE_AR llvm-ar CACHE FILEPATH "LLVM archiver")
set(CMAKE_RANLIB llvm-ranlib CACHE FILEPATH "LLVM ranlib")

execute_process(
        COMMAND ${CMAKE_C_COMPILER} -print-resource-dir
        OUTPUT_VARIABLE _astia_clang_resource_dir
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _astia_clang_resource_status
)
if (_astia_clang_resource_status)
    message(FATAL_ERROR "Unable to locate Clang resource headers")
endif ()

set(_astia_user_flags
        -target x86_64-unknown-elf
        -ffreestanding
        -fno-builtin
        -fno-pic
        -fno-pie
        -fno-stack-protector
        -fno-asynchronous-unwind-tables
        -mno-red-zone
        -nostdinc
        -isystem ${_astia_clang_resource_dir}/include
)

string(JOIN " " _astia_user_flags_string ${_astia_user_flags})
set(CMAKE_C_FLAGS_INIT "${_astia_user_flags_string}")
set(CMAKE_ASM_FLAGS_INIT "-target x86_64-unknown-elf -fno-pic -fno-pie")
set(CMAKE_EXE_LINKER_FLAGS_INIT
        "-target x86_64-unknown-elf -nostdlib -nostartfiles -nodefaultlibs -static -Wl,-no-pie"
)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
