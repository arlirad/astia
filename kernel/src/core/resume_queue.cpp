#include <astia/arch/cpu.hpp>
#include <astia/arch/platform.hpp>
#include <astia/arch/spinlock_guard.hpp>
#include <astia/atomic.hpp>
#include <astia/core/resume_queue.hpp>
#include <astia/resource_limits.hpp>

namespace astia::core {
    namespace {
        constexpr u32 max_cpus       = arch::platform::max_cpu_count;
        constexpr u32 queue_capacity = resource_limits::queue::resume;

        enum class slot_state : u8 {
            free,
            queued,
            running,
            canceled,
        };

        struct resume_slot {
            std::coroutine_handle<> continuation{};
            u64                     generation = 0;
            u8                      state      = static_cast<u8>(slot_state::free);
        };

        struct cpu_queue {
            spinlock                        lock;
            mpmc_queue<u16, queue_capacity> ready;
            mpmc_queue<u16, queue_capacity> overflow;
            resume_slot                     slots[queue_capacity * 2]{};
            std::coroutine_handle<>         active{};
            u32                             used = 0;
        };

        cpu_queue queues[max_cpus];
        bool      queues_initialized = false;

        bool is_state(resume_slot& slot, slot_state expected) {
            return astia::atomic_ref<u8>(slot.state).load(astia::memory_order::acquire) ==
                   static_cast<u8>(expected);
        }

        void release_slot(cpu_queue& queue, resume_slot& slot) {
            if (is_state(slot, slot_state::free))
                return;

            slot.continuation = {};
            slot.generation   = 0;

            astia::atomic_ref<u8>(slot.state)
                .store(static_cast<u8>(slot_state::free), astia::memory_order::release);

            if (queue.used != 0)
                --queue.used;
        }

        bool take_slot(cpu_queue& queue, u16& index) {
            if (queue.ready.try_pop(index))
                return true;

            return queue.overflow.try_pop(index);
        }

        void discard_canceled(cpu_queue& queue, u16 index) {
            resume_slot& slot = queue.slots[index];

            if (is_state(slot, slot_state::canceled))
                release_slot(queue, slot);
        }
    }

    void initialize_resume_queues() {
        if (astia::atomic_ref<bool>(queues_initialized)
                .load(astia::memory_order::acquire))
            return;

        for (cpu_queue& queue : queues) {
            queue.ready.initialize();
            queue.overflow.initialize();
        }

        astia::atomic_ref<bool>(queues_initialized)
            .store(true, astia::memory_order::release);
    }

    bool schedule_resume(std::coroutine_handle<> continuation, u64 generation) {
        if (!continuation)
            return false;

        if (!astia::atomic_ref<bool>(queues_initialized)
                 .load(astia::memory_order::acquire))
            return false;

        const u32 cpu = arch::cpu::current_index();
        if (cpu >= max_cpus)
            return false;

        cpu_queue& queue = queues[cpu];

        arch::interrupt_spinlock_guard guard(queue.lock);

        for (u16 i = 0; i < queue_capacity * 2; ++i) {
            resume_slot& slot = queue.slots[i];

            const auto state = static_cast<slot_state>(
                astia::atomic_ref<u8>(slot.state).load(astia::memory_order::acquire));

            if ((state == slot_state::queued || state == slot_state::running) &&
                slot.continuation.address() == continuation.address() &&
                slot.generation == generation)
                return true;
        }

        u16  free  = 0;
        bool found = false;

        for (u16 i = 0; i < queue_capacity * 2; ++i) {
            if (is_state(queue.slots[i], slot_state::free)) {
                free  = i;
                found = true;

                break;
            }
        }

        if (!found)
            return false;

        resume_slot& slot = queue.slots[free];

        slot.continuation = continuation;
        slot.generation   = generation;

        ++queue.used;
        astia::atomic_ref<u8>(slot.state)
            .store(static_cast<u8>(slot_state::queued), astia::memory_order::release);

        if (!queue.overflow.empty_approx()) {
            if (queue.overflow.try_push(free)) {
                arch::cpu::request_local_reschedule();

                return true;
            }

            release_slot(queue, slot);

            return false;
        }

        if (queue.ready.try_push(free) || queue.overflow.try_push(free)) {
            arch::cpu::request_local_reschedule();

            return true;
        }

        release_slot(queue, slot);

        return false;
    }

    void cancel_resume(std::coroutine_handle<> continuation, u64 generation) {
        if (!continuation)
            return;

        // Only online CPUs can own a queued handle.  max_cpus is the array
        // bound (256), so walking it made every task destruction scan all
        // 256 queues even on a single-CPU boot.
        u32 online = arch::platform::online_cpu_count();
        if (online == 0)
            online = 1;

        if (online > max_cpus)
            online = max_cpus;

        for (u32 cpu = 0; cpu < online; ++cpu) {
            cpu_queue& queue = queues[cpu];

            while (true) {
                arch::interrupt_spinlock_guard guard(queue.lock);

                if (queue.used == 0) {
                    guard.unlock();
                    break;
                }

                bool retry = false;

                for (u16 i = 0; i < queue_capacity * 2; ++i) {
                    resume_slot& slot = queue.slots[i];

                    const auto state =
                        static_cast<slot_state>(astia::atomic_ref<u8>(slot.state)
                                                    .load(astia::memory_order::acquire));

                    if (slot.continuation.address() != continuation.address())
                        continue;

                    if (generation != 0 && slot.generation != generation)
                        continue;

                    if (state == slot_state::queued) {
                        astia::atomic_ref<u8>(slot.state)
                            .store(static_cast<u8>(slot_state::canceled),
                                   astia::memory_order::release);
                    }
                    else if (state == slot_state::running) {
                        if (cpu == arch::cpu::current_index() &&
                            queue.active.address() == continuation.address())
                            continue;

                        retry = true;
                    }
                }

                guard.unlock();

                if (!retry)
                    break;

                while (true) {
                    bool running = false;

                    {
                        arch::interrupt_spinlock_guard wait_guard(queue.lock);

                        for (u16 i = 0; i < queue_capacity * 2; ++i) {
                            resume_slot& slot = queue.slots[i];

                            if (slot.continuation.address() == continuation.address() &&
                                (generation == 0 || slot.generation == generation) &&
                                is_state(slot, slot_state::running)) {
                                running = true;

                                break;
                            }
                        }
                    }

                    if (!running)
                        break;

                    arch::cpu::relax();
                }
            }
        }
    }

    u32 drain_resumes(u32 budget) {
        if (!astia::atomic_ref<bool>(queues_initialized)
                 .load(astia::memory_order::acquire))
            return 0;

        const u32 cpu = arch::cpu::current_index();
        if (cpu >= max_cpus)
            return 0;

        cpu_queue& queue = queues[cpu];

        u32 count = 0;

        while (count < budget) {
            u16 index = 0;

            {
                arch::interrupt_spinlock_guard guard(queue.lock);

                if (!take_slot(queue, index))
                    break;

                resume_slot& slot = queue.slots[index];

                if (is_state(slot, slot_state::canceled)) {
                    discard_canceled(queue, index);
                    continue;
                }

                astia::atomic_ref<u8>(slot.state)
                    .store(static_cast<u8>(slot_state::running),
                           astia::memory_order::release);

                queue.active = slot.continuation;
            }

            resume_slot& slot = queue.slots[index];

            slot.continuation.resume();

            {
                arch::interrupt_spinlock_guard guard(queue.lock);

                queue.active = {};

                release_slot(queue, slot);
            }

            ++count;
        }

        return count;
    }
} // namespace astia::core
