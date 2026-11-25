# astia userspace CRT0 (x86_64): the kernel's startup frame sits at %rsp
# on entry — argc, argv[], NULL, envp[], NULL.  Unpack it into main()'s
# arguments, run main, and leave through libc exit() so atexit handlers
# run and the stdio streams flush.
    .text
    .extern environ
    .globl _start
    .type _start, @function
_start:
    xor     %rbp, %rbp              # outermost frame: unwinding stops here
    movq    (%rsp), %rdi            # argc
    leaq    8(%rsp), %rsi           # argv
    movq    %rdi, %rax
    leaq    16(%rsp,%rax,8), %rdx   # envp = &argv[argc + 1]
    movq    environ@GOTPCREL(%rip), %rax   # GOT form: a PIE gets a GLOB_DAT
    movq    %rdx, (%rax)                   # POSIX libc global environment pointer
    and     $-16, %rsp              # SysV: 16-byte alignment at the call
    call    main
    movl    %eax, %edi
    call    exit
    hlt                             # exit does not return
    .size _start, .-_start
