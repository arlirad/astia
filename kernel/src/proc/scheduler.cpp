#include "scheduler.hpp"

#include "context.hpp"
#include "cpu.hpp"
#include "interrupts.hpp"
#include "share.hpp"
#include "proc.hpp"

namespace astia::proc::scheduler {

    namespace {
        inline constexpr u32 queue_capacity = 32;
        thread* queue[queue_capacity];
        u32 queue_count = 0;
        bool initialized = false;
        thread* idle_thread = nullptr;

        auto idle_entry(void*) -> void {
            for (;;) {
                arch::cpu::halt();
            }
        }

        auto enqueue(thread* value) -> bool {
            if (value == nullptr || queue_count == queue_capacity) {
                return false;
            }
            queue[queue_count++] = value;
            return true;
        }

        auto remove_at(u32 index) -> thread* {
            thread* value = queue[index];
            for (u32 i = index + 1; i < queue_count; ++i) {
                queue[i - 1] = queue[i];
            }
            --queue_count;
            return value;
        }

        auto select_next() -> thread* {
            if (queue_count == 0) {
                return nullptr;
            }

            // Realtime policy: strict priority, FIFO among equal priority.
            i32 realtime_index = -1;
            u8 realtime_priority = 0;
            for (u32 i = 0; i < queue_count; ++i) {
                thread* value = queue[i];
                if (value->state() != thread_state::runnable ||
                    value->sched_class() != scheduling_class::realtime) {
                    continue;
                }
                if (realtime_index < 0 || value->rt_priority() > realtime_priority) {
                    realtime_index = static_cast<i32>(i);
                    realtime_priority = value->rt_priority();
                }
            }
            if (realtime_index >= 0) {
                return remove_at(static_cast<u32>(realtime_index));
            }

            // Normal policy: lowest virtual runtime first.  nice affects
            // the charge applied on each timer tick, so a lower nice value
            // advances more slowly and receives more CPU time.
            i32 normal_index = -1;
            u64 lowest = ~u64{0};
            for (u32 i = 0; i < queue_count; ++i) {
                thread* value = queue[i];
                if (value->state() != thread_state::runnable) {
                    continue;
                }
                if (normal_index < 0 || value->vruntime() < lowest) {
                    normal_index = static_cast<i32>(i);
                    lowest = value->vruntime();
                }
            }
            return normal_index < 0 ? nullptr : remove_at(static_cast<u32>(normal_index));
        }

        auto nice_charge(i32 nice) -> u64 {
            // Integer approximation of a weighted fair scheduler: nice
            // -20 advances slowly, nice 19 advances quickly.
            return nice < 0 ? 1024u / static_cast<u32>(1 - nice)
                            : 1024u * static_cast<u32>(1 + nice);
        }
    } // namespace

    auto init() -> bool {
        initialized = proc::current_thread() != nullptr;
        if (!initialized || !arch::register_timer_hook(tick)) {
            return false;
        }
        auto idle = mm::share<thread>(proc::kernel_process(), proc::allocate_tid());
        if (!idle || !idle->prepare_kernel(idle_entry, nullptr) ||
            !proc::kernel_process()->add_thread(idle)) {
            return false;
        }
        idle->idle(true);
        idle->state(thread_state::runnable);
        idle_thread = idle.get();
        return true;
    }

    auto create_kernel(kernel_entry entry, void* argument,
                       scheduling_class sched_class, i32 nice,
                       u8 rt_priority) -> mm::shptr<thread> {
        if (!initialized || entry == nullptr || proc::kernel_process() == nullptr) {
            return {};
        }
        auto value = mm::share<thread>(proc::kernel_process(), proc::allocate_tid());
        if (!value || !value->prepare_kernel(entry, argument) ||
            !proc::kernel_process()->add_thread(value)) {
            return {};
        }
        value->sched_class(sched_class);
        value->nice(nice);
        value->rt_priority(rt_priority);
        value->state(thread_state::runnable);
        if (!enqueue(value.get())) {
            return {};
        }
        return value;
    }

    auto spawn_kernel(kernel_entry entry, void* argument,
                      scheduling_class sched_class, i32 nice,
                      u8 rt_priority) -> bool {
        return static_cast<bool>(create_kernel(entry, argument, sched_class,
                                               nice, rt_priority));
    }

    auto wake(thread* value) -> bool {
        if (value == nullptr || value->state() == thread_state::dead) {
            return false;
        }
        value->state(thread_state::runnable);
        return enqueue(value);
    }

    auto tick() -> void {
        thread* current = proc::current_thread();
        if (current != nullptr && current->sched_class() == scheduling_class::normal) {
            current->charge_runtime(nice_charge(current->nice()));
        }
        if (queue_count == 0) {
            return;
        }
        // Timer dispatch has already EOI'd the source. Open the gate
        // before switching so the next context receives normal IF state.
        arch::cpu::interrupts();
        yield();
    }

    auto yield() -> void {
        if (!initialized) {
            return;
        }
        thread* previous = proc::current_thread();
        if (previous == nullptr) {
            return;
        }

        if (!previous->idle() &&
            (previous->state() == thread_state::running ||
            previous->state() == thread_state::runnable)) {
            previous->state(thread_state::runnable);
            if (!enqueue(previous)) {
                previous->state(thread_state::running);
                return;
            }
        }

        thread* next = select_next();
        if (next == nullptr) {
            next = idle_thread;
        }
        if (next == nullptr) {
            if (previous->state() != thread_state::dead) {
                previous->state(thread_state::running);
            }
            return;
        }
        if (next == previous) {
            previous->state(thread_state::running);
            return;
        }
        next->state(thread_state::running);
        proc::set_current_thread(next);
        if (next->owner()->address_space() != nullptr) {
            next->owner()->address_space()->activate();
        } else if (arch::kernel_page_map() != nullptr) {
            arch::kernel_page_map()->activate();
        }
        if (next->kernel_stack_top() != 0) {
            arch::cpu::set_kernel_stack(next->kernel_stack_top());
        }
        arch::context_switch(previous->context(), next->context());
        // We are now running as `next`; `previous` (and any thread that
        // deferred its own destruction in finish()) can no longer run
        // on its kernel stack, so dead threads queued by finish() can
        // be destroyed safely here.
        proc::reap_dead_threads();
    }

    auto ready() -> bool { return initialized; }

} // namespace astia::proc::scheduler
