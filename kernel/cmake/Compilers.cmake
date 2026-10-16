# Compiler-specific freestanding/kernel settings
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # Freestanding: no host libc, no builtins, and critically no position
    # independent code / stack protector (there is no dynamic loader and
    # QEMU's -kernel loader expects a static ET_EXEC image).
    set(_astia_freestanding "-ffreestanding -fno-builtin -fno-pie -fno-pic -fno-stack-protector")

    # Interrupt handlers run on the interrupted thread's stack and overwrite
    # the 128-byte red zone below RSP, so no function may keep data there.
    set(_astia_freestanding "${_astia_freestanding} -mno-red-zone")

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${_astia_freestanding} -Wall -Wextra")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${_astia_freestanding} -Wall -Wextra")
    set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -fno-pie -fno-pic")

    # The kernel image links in the top 2 GiB of the higher half (image_base,
    # see cmake/linker/virt-x86_64.ld). The default -mcmodel=small assumes
    # zero-extendable *low* addresses (movl $sym, %reg), which truncates up
    # there; -mcmodel=kernel makes gcc use sign-extended addressing instead,
    # which is exactly what that window provides.
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mcmodel=kernel")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mcmodel=kernel")

    # Produce a static, non-PIE ELF that fits QEMU's -kernel loader.
    add_link_options(-nostdlib -nostartfiles -nodefaultlibs -no-pie -static -Wl,--build-id=none)
endif ()
