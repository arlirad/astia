/* Minimal freestanding C++ ABI support for the kernel image.
 * Kernel lifetime is explicit; module static-destructor registration is
 * replayed by the module lifecycle rather than process shutdown.
 */
#include <astia/arch/cpu.hpp>
#include <astia/atomic.hpp>

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
    if (astia::atomic_ref<unsigned char>(*state).load(astia::memory_order::acquire) == 2)
        return 0;
    unsigned char expected = 0;
    if (astia::atomic_ref<unsigned char>(*state).compare_exchange_strong(
            expected, 1, astia::memory_order::acquire, astia::memory_order::acquire))
        return 1;
    while (astia::atomic_ref<unsigned char>(*state).load(astia::memory_order::acquire) ==
           1)
        astia::arch::cpu::relax();
    return 0;
}

void __cxa_guard_release(long long* guard) {
    auto* state = reinterpret_cast<unsigned char*>(guard);
    astia::atomic_ref<unsigned char>(*state).store(static_cast<unsigned char>(2),
                                                   astia::memory_order::release);
}

void __cxa_guard_abort(long long* guard) {
    auto* state = reinterpret_cast<unsigned char*>(guard);
    astia::atomic_ref<unsigned char>(*state).store(static_cast<unsigned char>(0),
                                                   astia::memory_order::release);
}

[[noreturn]] void __cxa_pure_virtual() {
    for (;;) {
        astia::arch::cpu::halt();
    }
}
}
