/* Minimal freestanding C++ ABI support for the kernel image.
 * Kernel lifetime is explicit; module static-destructor registration is
 * replayed by the module lifecycle rather than process shutdown.
 */
extern "C" int  astia_module_register_atexit(void (*)(void));
extern "C" int  astia_module_register_cxa_atexit(void (*)(void*), void*, void*);
extern "C" void astia_module_finalize_cxa(void*);

extern "C" {
void* __dso_handle = &__dso_handle;

int __cxa_atexit(void (*function)(void*), void* argument, void* dso_handle) {
    return astia_module_register_cxa_atexit(function, argument, dso_handle);
}

int atexit(void (*function)(void)) {
    return astia_module_register_atexit(function);
}

void __cxa_finalize(void* dso_handle) {
    astia_module_finalize_cxa(dso_handle);
}

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
