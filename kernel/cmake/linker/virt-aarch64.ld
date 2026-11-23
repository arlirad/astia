OUTPUT_FORMAT(elf64-littleaarch64)
OUTPUT_ARCH(aarch64)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x40400000;

    .text : ALIGN(16)
    {
        KEEP(*(.text.boot))
        *(.text .text.*)
    } :text

    .rodata : ALIGN(16)
    {
        *(.rodata .rodata.*)
    } :rodata

    .data : ALIGN(16)
    {
        *(.data .data.*)
    } :data

    .bss (NOLOAD) : ALIGN(16)
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(16);
        __stack_bottom = .;
        . += 0x8000;
        __stack_top = .;
        __bss_end = .;
    } :data

    /DISCARD/ :
    {
        *(.comment)
        *(.note .note.*)
        *(.eh_frame .eh_frame.*)
        *(.ARM.exidx .ARM.exidx.*)
        *(.ARM.attributes)
        *(.interp)
        *(.dynamic)
        *(.dynsym)
        *(.dynstr)
        *(.hash)
        *(.gnu.hash)
        *(.rela .rela.*)
        *(.got .got.*)
    }
}
