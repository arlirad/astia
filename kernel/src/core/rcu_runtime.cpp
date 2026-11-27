#include <astia/core/rcu_runtime.hpp>

void* operator new(astia::usz size, void* where) noexcept;

namespace astia::core {
    system_rcu_type& system_rcu_domain() {
        alignas(system_rcu_type) static u8 storage[sizeof(system_rcu_type)];

        static bool initialized = false;
        if (!initialized) {
            ::new (storage) system_rcu_type{};

            initialized = true;
        }

        return *reinterpret_cast<system_rcu_type*>(storage);
    }

    void system_rcu_quiescent(u32 cpu) {
        system_rcu_domain().quiescent(cpu);
    }

    u32 system_rcu_poll() {
        return system_rcu_domain().poll();
    }
} // namespace astia::core
