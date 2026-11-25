SECTIONS {
    .astia_syscall_handlers : {
        __start_astia_syscall_handlers = .;
        KEEP(*(astia_syscall_handlers))
        __stop_astia_syscall_handlers = .;
    }
}
INSERT AFTER .rodata;
