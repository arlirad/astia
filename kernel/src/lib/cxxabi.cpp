extern "C" {
void* __dso_handle __attribute__((weak, visibility("hidden"))) = nullptr;

namespace {

    constexpr unsigned max_atexit_entries = 256;

    struct atexit_entry {
        void (*function)(void*);
        void* argument;
        void* dso_handle;
        bool  used;
        bool  finalized;
    };

    atexit_entry entries[max_atexit_entries] = {};
    unsigned     entry_count                 = 0;
    unsigned     lock_state                  = 0;

    void lock() {
        for (;;) {
            unsigned expected = 0;
            if (__atomic_compare_exchange_n(&lock_state, &expected, 1, false,
                                            __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
                return;
            asm volatile("pause" ::: "memory");
        }
    }

    void unlock() {
        __atomic_store_n(&lock_state, 0, __ATOMIC_RELEASE);
    }

} // namespace

int __cxa_atexit(void (*function)(void*), void* argument, void* dso_handle) {
    if (function == nullptr)
        return -1;
    lock();
    if (entry_count == max_atexit_entries) {
        unlock();
        return -1;
    }
    entries[entry_count++] = {function, argument, dso_handle, true, false};
    unlock();
    return 0;
}

void __cxa_finalize(void* dso_handle) {
    for (;;) {
        atexit_entry entry{};
        bool         found = false;
        lock();
        for (unsigned i = entry_count; i != 0; --i) {
            atexit_entry& candidate = entries[i - 1];
            if (candidate.used && !candidate.finalized &&
                (dso_handle == nullptr || candidate.dso_handle == dso_handle)) {
                candidate.finalized = true;
                entry               = candidate;
                found               = true;
                break;
            }
        }
        unlock();
        if (!found)
            return;
        entry.function(entry.argument);
    }
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
