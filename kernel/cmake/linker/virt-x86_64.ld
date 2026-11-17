ENTRY(_start)

/* Virtual base of the kernel image; every section below carries an explicit
 * AT() so its physical (LMA) address is the same offset from 0. Must match
 * astia::arch::image_base (src/arch/x86_64/layout.cpp) and the boot page
 * tables (src/boot/virt/boot.S). */
kernel_image_base = 0xFFFFFFFF80000000;

SECTIONS
{
    /* AP reset vectors must be below 1 MiB. The frame allocator reserves
     * this region and the boot identity map covers it. */
    . = 0x8000;
    .ap_trampoline : {
        KEEP(*(.ap_trampoline))
    }

    /* Physical base. QEMU's PVH loader reads segments at their p_paddr and
     * jumps to the physical entry from the .note.Xen descriptor. */
    . = 1M;
    __kernel_start = .;

    /* Multiboot2 requires the complete header within the first 32768 bytes
     * and on an 8-byte boundary. */
    .multiboot2 : ALIGN(8) {
        KEEP(*(.multiboot2))
    }
    ASSERT(SIZEOF(.multiboot2) > 0, "missing Multiboot2 header")
    ASSERT(ADDR(.multiboot2) - __kernel_start < 32768,
           "Multiboot2 header is outside first 32 KiB")

    /* Must precede the .note collection below. */
    /DISCARD/ : {
        *(.comment)
        *(.note.GNU-stack)
        *(.note.gnu.property)
    }

    /* .note.Xen holds the PVH entry point used by QEMU -kernel.
     * Alignment/type must stay SHT_NOTE so a PT_NOTE segment is emitted. */
    .note : ALIGN(4) {
        KEEP(*(.note))
        KEEP(*(.note.*))
    }

    . = ALIGN(4096);

    /* Boot code and data, identity-linked (VMA == LMA): the 32-bit entry
     * addresses tables/stack/GDT through truncated 32-bit pointers. */
    .boot.text : {
        *(.boot.text)
        *(.boot.text.*)
    }

    . = ALIGN(4096);

    .boot.rodata : {
        *(.boot.rodata)
        *(.boot.rodata.*)
    }

    . = ALIGN(4096);

    .boot.bss : {
        *(.boot.bss)
        *(.boot.bss.*)
    }

    . = ALIGN(4096);

    /* Jump to the higher half: from here VMA = image_base + physical, while
     * the explicit AT() keeps the physical load addresses continuous from
     * where the boot sections end. */
    . += kernel_image_base;

    .text : AT(ADDR(.text) - kernel_image_base) {
        *(.text)
        *(.text.*)
    }

    . = ALIGN(4096);

    /* .eh_frame is collected here on purpose: left as an orphan it would
     * pick up its LMA from the high half of the location counter. */
    .rodata : AT(ADDR(.rodata) - kernel_image_base) {
        *(.rodata)
        *(.rodata.*)
        *(.eh_frame)
        *(.eh_frame.*)
    }

    /* Filled after the final link from this image's own ELF symtab. The
     * reserved bytes keep the linked addresses stable while the build tool
     * writes the exact names and addresses into the loadable image. */
    . = ALIGN(8);
    .kernel_symbols : AT(ADDR(.kernel_symbols) - kernel_image_base) {
        __kernel_symbol_table_start = .;
        FILL(0)
        BYTE(0)
        . += 524287;
        __kernel_symbol_table_end = .;
    }

    /* Init steps registered with ASTIA_INIT_STEP* land here. KEEP guards them
     * against --gc-sections; the bounds are what astia::init::run() walks. */
    . = ALIGN(8);
    .initcall : AT(ADDR(.initcall) - kernel_image_base) {
        __init_steps_start = .;
        KEEP(*(.initcall))
        __init_steps_end = .;
    }

    . = ALIGN(4096);

    .data : AT(ADDR(.data) - kernel_image_base) {
        *(.data)
        *(.data.*)
    }

    . = ALIGN(4096);

    .bss : AT(ADDR(.bss) - kernel_image_base) {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }

    . = ALIGN(4096);

    /* __kernel_start / __kernel_end stay *physical*: the frame allocator
     * reserves [start, end) in physical memory and parks its bitmap right
     * after end. The arch maps that range at image_base + phys. */
    __kernel_end = . - kernel_image_base;
}
