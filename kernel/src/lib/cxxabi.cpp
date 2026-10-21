/* Minimal freestanding C++ ABI support for the kernel image.
 * Kernel lifetime is explicit; static-destructor registration is accepted but
 * intentionally not replayed during shutdown because the kernel never exits.
 */
extern "C" {
void* __dso_handle = nullptr;

int __cxa_atexit(void (*)(void*), void*, void*) {
    return 0;
}

void __cxa_finalize(void*) {}

[[noreturn]] void __cxa_pure_virtual() {
    for (;;) {
        asm volatile("cli; hlt");
    }
}
}
