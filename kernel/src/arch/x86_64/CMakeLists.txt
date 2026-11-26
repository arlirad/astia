add_library(arch_x86_64 STATIC
        address_space.cpp
        page_map.cpp
        layout.cpp
        identity.cpp
        cpu.cpp
        interrupts.cpp
        pic.cpp
        pit.cpp
        lapic.cpp
        context.S
        user.S
        syscall.S
        context.cpp
        gdt.cpp
        gdt.S
        ap_trampoline.S
        isrs.S
        ${CMAKE_CURRENT_BINARY_DIR}/isrs_gen.S
        init.cpp
        pci_config.cpp
        pci_mmio_owner.cpp
        smp.cpp
        platform.cpp
        entropy.cpp
        panic.cpp
        panic_format.cpp
)

# The 256 per-vector entry stubs (and their address table) are mechanical;
# gen_isrs.sh emits them so the error-code/vector bookkeeping lives in one
# place instead of 256 hand-written blocks.
add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/isrs_gen.S
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/gen_isrs.sh ${CMAKE_CURRENT_BINARY_DIR}/isrs_gen.S
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gen_isrs.sh
        COMMENT "Generating ISR stubs isrs_gen.S"
        VERBATIM
)

target_include_directories(arch_x86_64 PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ASTIA_KERNEL_SOURCE_DIR}/src/log
)

target_link_libraries(arch_x86_64 PUBLIC
        libcore
        mm
        dev
        log
)
