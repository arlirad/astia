add_library(dev STATIC
        device.cpp
        blk/blkdev.cpp
        device_file.cpp
        console.cpp
        driver.cpp
        chr/tty.cpp
        chr/tty/pty.cpp
        chr/tty/pty_vfs.cpp
        chr/tty/virtual_tty.cpp
        chr/tty/virtual_tty_register.cpp
        chr/tty/psf_font.cpp
        chr/tty/framebuffer_renderer.cpp
        chr/tty/framebuffer_console.cpp
        chr/pseudo.cpp
        chr/pseudo_register.cpp
        chr/tty/uart16550.cpp
        debug_exit.cpp
        init_dev.cpp
)

find_program(ASTIA_TTV0_OBJCOPY NAMES llvm-objcopy objcopy REQUIRED)

set(ASTIA_TTV0_FONT_OBJECT ${CMAKE_CURRENT_BINARY_DIR}/aex2_font.psf.o)
add_custom_command(
        OUTPUT ${ASTIA_TTV0_FONT_OBJECT}
        COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_CURRENT_SOURCE_DIR}/chr/tty
                ${ASTIA_TTV0_OBJCOPY} -B i386:x86-64 -O elf64-x86-64 -I binary
                aex2_font.psf ${ASTIA_TTV0_FONT_OBJECT}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/chr/tty/aex2_font.psf
        VERBATIM
)
target_sources(dev PRIVATE ${ASTIA_TTV0_FONT_OBJECT})

target_include_directories(dev PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ASTIA_KERNEL_SOURCE_DIR}/include
)

target_link_libraries(dev PUBLIC libcore arch_${ARCH} sys hw)
