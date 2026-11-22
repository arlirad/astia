#include <astia/arch/cpu.hpp>
#include <astia/arch/platform.hpp>
#include <astia/core/coroutine_lifecycle.hpp>
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
            u64                     key        = 0;
            u8                      state      = static_cast<u8>(slot_state::free);
            u8                      ref_held   = 0;
        };

        struct cpu_queue {
            spinlock                        lock;
            mpmc_queue<u16, queue_capacity> ready;
            mpmc_queue<u16, queue_capacity> overflow;
            resume_slot                     slots[queue_capacity * 2]{};
            std::coroutine_handle<>         active{};
        };

        cpu_queue queues[max_cpus];
        bool      queues_initialized = false;

        bool is_state(const resume_slot& slot, slot_state expected) {
            return __atomic_load_n(&slot.state, __ATOMIC_ACQUIRE) ==
                   static_cast<u8>(expected);
        }

        void release_slot(resume_slot& slot) {
            slot.continuation = {};
            slot.generation   = 0;
            slot.key          = 0;
            slot.ref_held     = 0;
            __atomic_store_n(&slot.state, static_cast<u8>(slot_state::free),
                             __ATOMIC_RELEASE);
        }

        bool take_slot(cpu_queue& queue, u16& index) {
            if (queue.ready.try_pop(index))
                return true;
            return queue.overflow.try_pop(index);
        }

        void discard_canceled(cpu_queue& queue, u16 index) {
            resume_slot& slot = queue.slots[index];
            if (is_state(slot, slot_state::canceled))
                release_slot(slot);
        }
    }

    void initialize_resume_queues() {
        if (__atomic_load_n(&queues_initialized, __ATOMIC_ACQUIRE))
            return;
        for (cpu_queue& queue : queues) {
            queue.ready.initialize();
            queue.overflow.initialize();
        }
        __atomic_store_n(&queues_initialized, true, __ATOMIC_RELEASE);
    }

    resume_schedule schedule_resume_owned(std::coroutine_handle<> continuation,
                                          u64 generation, u64 key) {
        if (!continuation || !__atomic_load_n(&queues_initialized, __ATOMIC_ACQUIRE))
            return resume_schedule::failed;
        const u32 cpu = arch::cpu::current_index();
        if (cpu >= max_cpus)
            return resume_schedule::failed;

        cpu_queue&                     queue = queues[cpu];
        arch::interrupt_spinlock_guard guard(queue.lock);
        for (u16 i = 0; i < queue_capacity * 2; ++i) {
            resume_slot& slot = queue.slots[i];
            const auto   state =
                static_cast<slot_state>(__atomic_load_n(&slot.state, __ATOMIC_ACQUIRE));
            if ((state == slot_state::queued || state == slot_state::running) &&
                slot.continuation.address() == continuation.address() &&
                slot.generation == generation && slot.key == key)
                return resume_schedule::duplicate;
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
            return resume_schedule::failed;

        resume_slot& slot = queue.slots[free];
        slot.continuation = continuation;
        slot.generation   = generation;
        slot.key          = key;
        slot.ref_held     = 1;
        __atomic_store_n(&slot.state, static_cast<u8>(slot_state::queued),
                         __ATOMIC_RELEASE);
        if (!queue.overflow.empty_approx()) {
            if (queue.overflow.try_push(free)) {
                arch::cpu::request_local_reschedule();
                return resume_schedule::queued;
            }
            release_slot(slot);
            return resume_schedule::failed;
        }
        if (queue.ready.try_push(free) || queue.overflow.try_push(free)) {
            arch::cpu::request_local_reschedule();
            return resume_schedule::queued;
        }

        release_slot(slot);
        return resume_schedule::failed;
    }

    bool schedule_resume(std::coroutine_handle<> continuation, u64 generation, u64 key) {
        if (!continuation)
            return false;
        if (!resume_begin(continuation))
            return false;
        const resume_schedule result =
            schedule_resume_owned(continuation, generation, key);
        if (result != resume_schedule::queued)
            resume_end(continuation);
        return result != resume_schedule::failed;
    }

    void cancel_resume(std::coroutine_handle<> continuation, u64 generation, u64 key) {
        if (!continuation)
            return;
        for (u32 cpu = 0; cpu < max_cpus; ++cpu) {
            cpu_queue& queue = queues[cpu];
            for (;;) {
                arch::interrupt_spinlock_guard guard(queue.lock);
                bool                           retry    = false;
                u32                            released = 0;
                for (u16 i = 0; i < queue_capacity * 2; ++i) {
                    resume_slot& slot  = queue.slots[i];
                    const auto   state = static_cast<slot_state>(
                        __atomic_load_n(&slot.state, __ATOMIC_ACQUIRE));
                    if (slot.continuation.address() != continuation.address())
                        continue;
                    if (generation != 0 && slot.generation != generation)
                        continue;
                    if (key != 0 && slot.key != key)
                        continue;
                    if (state == slot_state::queued) {
                        __atomic_store_n(&slot.state,
                                         static_cast<u8>(slot_state::canceled),
                                         __ATOMIC_RELEASE);
                        if (slot.ref_held != 0) {
                            slot.ref_held = 0;
                            ++released;
                        }
                    }
                    else if (state == slot_state::running) {
                        if (cpu == arch::cpu::current_index() &&
                            queue.active.address() == continuation.address())
                            continue;
                        retry = true;
                    }
                }
                guard.unlock();
                while (released != 0) {
                    resume_end(continuation);
                    --released;
                }
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
                                (key == 0 || slot.key == key) &&
                                is_state(slot, slot_state::running)) {
                                running = true;
                                break;
                            }
                        }
                    }
                    if (!running)
                        break;
                    ASTIA_CPU_RELAX();
                }
            }
        }
    }

    u32 drain_resumes(u32 budget) {
        if (!__atomic_load_n(&queues_initialized, __ATOMIC_ACQUIRE))
            return 0;
        const u32 cpu = arch::cpu::current_index();
        if (cpu >= max_cpus)
            return 0;
        cpu_queue& queue = queues[cpu];
        u32        count = 0;
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
                __atomic_store_n(&slot.state, static_cast<u8>(slot_state::running),
                                 __ATOMIC_RELEASE);
                queue.active = slot.continuation;
            }
            resume_slot& slot = queue.slots[index];
            slot.continuation.resume();
            resume_end(slot.continuation);
            {
                arch::interrupt_spinlock_guard guard(queue.lock);
                queue.active = {};
                release_slot(slot);
            }
            ++count;
        }
        return count;
    }
} // namespace astia::core
