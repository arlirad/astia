#pragma once

#include <astia/atomic.hpp>
#include <astia/common.hpp>
#include <astia/resource_limits.hpp>

namespace astia::core {

    /// Bounded epoch-based RCU domain. Readers are lockless; writers retire
    /// objects and call poll() from a safe maintenance context. A reader must
    /// not block or migrate CPUs while it is active.
    template <u32 CpuCount       = 256,
              u32 RetireCapacity = resource_limits::queue::rcu_retired>
    class rcu_domain {
        static_assert(CpuCount != 0 && RetireCapacity != 0);

        struct retired_item {
            void* value            = nullptr;
            void (*destroy)(void*) = nullptr;
            u64 epoch              = 0;
        };
        struct cpu_state {
            u32          nesting = 0;
            u64          epoch   = 0;
            retired_item retired[RetireCapacity]{};
            u32          retired_count = 0;
        };

        alignas(cache_line_bytes) mutable u64 _epoch = 1;
        cpu_state _cpus[CpuCount]{};

        bool grace_complete(u64 retired_epoch) const {
            for (u32 cpu = 0; cpu < CpuCount; ++cpu) {
                const cpu_state& state = _cpus[cpu];
                if (state.nesting != 0 && state.epoch <= retired_epoch)
                    return false;
            }
            return true;
        }

        public:
        class read_guard {
            public:
            read_guard(rcu_domain& domain, u32 cpu) : _domain(domain), _cpu(cpu) {
                if (_cpu >= CpuCount)
                    return;
                cpu_state& state = _domain._cpus[_cpu];
                if (state.nesting++ == 0)
                    state.epoch = astia::atomic_ref<u64>(_domain._epoch)
                                      .load(astia::memory_order::seq_cst);
                _active = true;
            }
            ~read_guard() {
                if (_active)
                    --_domain._cpus[_cpu].nesting;
            }
            read_guard(const read_guard&)            = delete;
            read_guard& operator=(const read_guard&) = delete;

            bool active() const {
                return _active;
            }

            private:
            rcu_domain& _domain;
            u32         _cpu;
            bool        _active = false;
        };

        /// Retire an object after removing it from the published structure.
        /// Returns false when the bounded retire queue is full.
        bool retire(u32 cpu, void* value, void (*destroy)(void*)) {
            if (cpu >= CpuCount || value == nullptr || destroy == nullptr)
                return false;
            cpu_state& state = _cpus[cpu];
            if (state.retired_count == RetireCapacity)
                return false;
            state.retired[state.retired_count++] = {
                value, destroy,
                astia::atomic_ref<u64>(_epoch).load(astia::memory_order::seq_cst)};
            return true;
        }

        bool retire(void* value, void (*destroy)(void*)) {
            return retire(0, value, destroy);
        }

        /// Advance the epoch and reclaim objects whose pre-existing readers
        /// have all left. Destructors run outside any lock by construction.
        u32 poll() {
            astia::atomic_ref<u64>(_epoch).add_fetch(1, astia::memory_order::seq_cst);
            u32 reclaimed = 0;
            for (u32 cpu = 0; cpu < CpuCount; ++cpu) {
                cpu_state& state = _cpus[cpu];
                u32        write = 0;
                for (u32 read = 0; read < state.retired_count; ++read) {
                    retired_item item = state.retired[read];
                    if (grace_complete(item.epoch)) {
                        item.destroy(item.value);
                        ++reclaimed;
                    }
                    else {
                        state.retired[write++] = item;
                    }
                }
                state.retired_count = write;
            }
            return reclaimed;
        }

        u32 pending() const {
            u32 total = 0;
            for (u32 cpu = 0; cpu < CpuCount; ++cpu)
                total += _cpus[cpu].retired_count;
            return total;
        }
        void quiescent(u32 cpu) {
            if (cpu < CpuCount && _cpus[cpu].nesting == 0)
                _cpus[cpu].epoch =
                    astia::atomic_ref<u64>(_epoch).load(astia::memory_order::seq_cst);
        }
        u64 epoch() const {
            return astia::atomic_ref<u64>(_epoch).load(astia::memory_order::seq_cst);
        }
    };

} // namespace astia::core
