#pragma once

#include <astia/common.hpp>

namespace astia::core {

    /// Bounded epoch-based RCU domain. Readers are lockless; writers retire
    /// objects and call poll() from a safe maintenance context. A reader must
    /// not block or migrate CPUs while it is active.
    template <u32 CpuCount = 256, u32 RetireCapacity = 256>
    class rcu_domain {
        static_assert(CpuCount != 0 && RetireCapacity != 0);

        struct retired_item {
            void* value = nullptr;
            void (*destroy)(void*) = nullptr;
            u64 epoch = 0;
        };
        struct cpu_state {
            u32 nesting = 0;
            u64 epoch   = 0;
            retired_item retired[RetireCapacity]{};
            u32 retired_count = 0;
        };

        alignas(cache_line_bytes) u64 epoch_ = 1;
        cpu_state cpus_[CpuCount]{};

        bool grace_complete(u64 retired_epoch) const {
            for (u32 cpu = 0; cpu < CpuCount; ++cpu) {
                const cpu_state& state = cpus_[cpu];
                if (state.nesting != 0 && state.epoch <= retired_epoch)
                    return false;
            }
            return true;
        }

        public:
        class read_guard {
            public:
            read_guard(rcu_domain& domain, u32 cpu) : domain_(domain), cpu_(cpu) {
                if (cpu_ >= CpuCount)
                    return;
                cpu_state& state = domain_.cpus_[cpu_];
                if (state.nesting++ == 0)
                    state.epoch = __atomic_load_n(&domain_.epoch_, __ATOMIC_SEQ_CST);
                active_ = true;
            }
            ~read_guard() {
                if (active_)
                    --domain_.cpus_[cpu_].nesting;
            }
            read_guard(const read_guard&) = delete;
            read_guard& operator=(const read_guard&) = delete;

            bool active() const { return active_; }

            private:
            rcu_domain& domain_;
            u32          cpu_;
            bool         active_ = false;
        };

        /// Retire an object after removing it from the published structure.
        /// Returns false when the bounded retire queue is full.
        bool retire(u32 cpu, void* value, void (*destroy)(void*)) {
            if (cpu >= CpuCount || value == nullptr || destroy == nullptr)
                return false;
            cpu_state& state = cpus_[cpu];
            if (state.retired_count == RetireCapacity)
                return false;
            state.retired[state.retired_count++] = {value, destroy,
                                                    __atomic_load_n(&epoch_, __ATOMIC_SEQ_CST)};
            return true;
        }

        bool retire(void* value, void (*destroy)(void*)) {
            return retire(0, value, destroy);
        }

        /// Advance the epoch and reclaim objects whose pre-existing readers
        /// have all left. Destructors run outside any lock by construction.
        u32 poll() {
            __atomic_add_fetch(&epoch_, 1, __ATOMIC_SEQ_CST);
            u32 reclaimed = 0;
            for (u32 cpu = 0; cpu < CpuCount; ++cpu) {
                cpu_state& state = cpus_[cpu];
                u32 write = 0;
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
                total += cpus_[cpu].retired_count;
            return total;
        }
        void quiescent(u32 cpu) {
            if (cpu < CpuCount && cpus_[cpu].nesting == 0)
                cpus_[cpu].epoch = __atomic_load_n(&epoch_, __ATOMIC_SEQ_CST);
        }
        u64 epoch() const { return __atomic_load_n(&epoch_, __ATOMIC_SEQ_CST); }
    };

} // namespace astia::core
