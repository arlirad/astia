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

int __cxa_guard_acquire(long long* guard) {
    auto* state = reinterpret_cast<unsigned char*>(guard);
    if (__atomic_load_n(state, __ATOMIC_ACQUIRE) == 2)
        return 0;
    unsigned char expected = 0;
    if (__atomic_compare_exchange_n(state, &expected, 1, false, __ATOMIC_ACQUIRE,
                                    __ATOMIC_ACQUIRE))
        return 1;
    while (__atomic_load_n(state, __ATOMIC_ACQUIRE) == 1)
        asm volatile("pause" ::: "memory");
    return 0;
}

void __cxa_guard_release(long long* guard) {
    auto* state = reinterpret_cast<unsigned char*>(guard);
    __atomic_store_n(state, static_cast<unsigned char>(2), __ATOMIC_RELEASE);
}

void __cxa_guard_abort(long long* guard) {
    auto* state = reinterpret_cast<unsigned char*>(guard);
    __atomic_store_n(state, static_cast<unsigned char>(0), __ATOMIC_RELEASE);
}

[[noreturn]] void __cxa_pure_virtual() {
    for (;;) {
        asm volatile("cli; hlt");
    }
}
}
