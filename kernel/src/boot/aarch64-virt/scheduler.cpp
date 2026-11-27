#include "scheduler.hpp"

#include <astia/arch/cpu.hpp>
#include <astia/arch/timer.hpp>

#include "context.hpp"
#include "irq.hpp"
#include "timer.hpp"

namespace astia::boot::aarch64::virt::sched {
    namespace {
        arch::context  boot_context{};
        arch::context  contexts[policy::max_threads]{};
        policy::thread records[policy::max_threads]{};

        alignas(16) u8 stacks[policy::max_threads][stack_bytes]{};

        i32      current     = -1;
        bool     initialized = false;
        bool     active      = false;
        evidence summary{};

        [[nodiscard]] bool valid_id(i32 id) {
            return id >= 0 && static_cast<u32>(id) < policy::max_threads;
        }

        [[nodiscard]] arch::context* context_for(i32 id) {
            return id < 0 ? &boot_context : &contexts[id];
        }

        void switch_to(i32 next) {
            const i32 previous = current;

            if (next == previous)
                return;

            if (next >= 0)
                records[next].state = policy::thread_state::running;

            current = next;

            arch::cpu::nointerrupts();
            arch::context_switch(context_for(previous), context_for(next));
        }

        [[noreturn]] void retire() {
            if (valid_id(current)) {
                records[current].state = policy::thread_state::finished;

                ++summary.retires;
            }

            const i32 next = policy::next_runnable(records, policy::max_threads, current);

            switch_to(next);

            while (true) {
                arch::cpu::halt();
            }
        }

        void deadline_wake(void* context) {
            auto* value = static_cast<policy::thread*>(context);

            if (value == nullptr || !policy::wake(*value)) {
                ++summary.spurious_wakes;

                return;
            }

            ++summary.wakes;
            ++summary.deadline_wakes;

            arch::cpu::request_local_reschedule();
        }
    } // namespace

    bool init() {
        if (initialized)
            return false;

        for (u32 index = 0; index < policy::max_threads; ++index) {
            contexts[index] = arch::context{};
            records[index]  = policy::thread{};
        }

        boot_context = arch::context{};
        current      = -1;
        active       = false;
        summary      = evidence{};

        if (arch::aarch64::irq::set_handoff(reschedule_from_irq) !=
            arch::aarch64::irq::handoff_error::none)
            return false;

        if (!arch::register_timer_hook(timer_tick)) {
            (void) arch::aarch64::irq::clear_handoff();

            return false;
        }

        initialized = true;

        return true;
    }

    i32 create(entry function, void* argument) {
        if (!initialized || function == nullptr)
            return -1;

        for (u32 index = 0; index < policy::max_threads; ++index) {
            if (records[index].state != policy::thread_state::free)
                continue;

            const u64 stack_top = reinterpret_cast<u64>(stacks[index]) + stack_bytes;

            if (!arch::context_prepare_kernel_entry(&contexts[index], stack_top,
                                                    reinterpret_cast<u64>(function),
                                                    reinterpret_cast<u64>(argument)))
                return -1;

            records[index]       = policy::thread{};
            records[index].state = policy::thread_state::runnable;

            ++summary.created;

            return static_cast<i32>(index);
        }

        return -1;
    }

    bool start() {
        if (!initialized || active)
            return false;

        active = true;

        yield();

        active = false;

        return true;
    }

    bool sleep_until(u64 deadline_ms) {
        if (!active || !valid_id(current))
            return false;

        const u64 now = astia::arch::aarch64::timer::now_ms();

        if (deadline_ms <= now || deadline_ms - now > 0xffffffffull)
            return false;

        arch::interrupt_guard gate;

        u32 deadline_id = 0;

        if (astia::arch::aarch64::timer::schedule_deadline(
                now, static_cast<u32>(deadline_ms - now), deadline_wake,
                &records[current],
                deadline_id) != astia::arch::aarch64::timer::deadline_error::none)
            return false;

        if (!policy::sleep_until(records[current], deadline_ms)) {
            (void) astia::arch::aarch64::timer::cancel_deadline(deadline_id);

            return false;
        }

        ++summary.sleeps;

        yield();

        return true;
    }

    void yield() {
        if (!active)
            return;

        arch::interrupt_guard gate;

        if (valid_id(current) && records[current].state == policy::thread_state::running)
            records[current].state = policy::thread_state::runnable;

        const i32 next = policy::next_runnable(records, policy::max_threads, current);
        if (next != current)
            switch_to(next);

        gate.unlock();
    }

    void reschedule_from_irq() {
        if (!active || !arch::cpu::consume_should_yield())
            return;

        yield();
    }

    void timer_tick() {
        if (!active)
            return;

        const u64 now  = astia::arch::aarch64::timer::now_ms();
        u32       woke = 0;

        for (u32 index = 0; index < policy::max_threads; ++index) {
            if (policy::wake_if_due(records[index], now))
                ++woke;
        }

        if (woke == 0)
            return;

        summary.wakes += woke;
        summary.deadline_wakes += woke;

        arch::cpu::request_local_reschedule();
    }

    const evidence& stats() {
        return summary;
    }

    policy::thread_state thread_state(i32 id) {
        return valid_id(id) ? records[id].state : policy::thread_state::free;
    }

    u32 wake_count(i32 id) {
        return valid_id(id) ? records[id].wake_count : 0;
    }

    bool finished(i32 id) {
        return valid_id(id) && policy::finished(records[id]);
    }

    bool sleeping(i32 id) {
        return valid_id(id) && policy::sleeping(records[id]);
    }

    extern "C" void astia_aarch64_context_start(void (*function)(void*), void* argument) {
        arch::cpu::interrupts();

        if (valid_id(current) &&
            records[current].state == policy::thread_state::running) {
            records[current].started = true;

            ++summary.started;
        }

        function(argument);
    }

    extern "C" [[noreturn]] void astia_aarch64_context_returned() {
        retire();
    }
} // namespace astia::boot::aarch64::virt::sched
