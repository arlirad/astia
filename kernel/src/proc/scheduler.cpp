#include "scheduler.hpp"

#include "proc.hpp"
#include <astia/arch/context.hpp>
#include <astia/arch/cpu.hpp>
#include <astia/arch/interrupts.hpp>
#include <astia/mm/share.hpp>

namespace astia::proc::scheduler {

    namespace {
        inline constexpr u32 queue_capacity = 32;
        thread*              queue[queue_capacity];
        u32                  queue_count = 0;
        bool                 initialized = false;
        thread*              idle_thread = nullptr;

        void idle_entry(void*) {
            // Entered by a switch like any other thread, and the switcher's
            // gate is not ours to keep: open it so the next tick can take
            // the CPU back out of the halt below.
            arch::cpu::interrupts();
            for (;;) {
                arch::cpu::halt();
            }
        }

        bool enqueue(thread* value) {
            if (value == nullptr) {
                return false;
            }
            if (value->queued()) {
                return true;
            }
            if (queue_count == queue_capacity) {
                return false;
            }
            value->queued(true);
            queue[queue_count++] = value;
            return true;
        }

        thread* remove_at(u32 index) {
            thread* value = queue[index];
            value->queued(false);
            for (u32 i = index + 1; i < queue_count; ++i) {
                queue[i - 1] = queue[i];
            }
            --queue_count;
            return value;
        }

        thread* select_next() {
            if (queue_count == 0) {
                return nullptr;
            }

            // Realtime policy: strict priority, FIFO among equal priority.
            i32 realtime_index    = -1;
            u8  realtime_priority = 0;
            for (u32 i = 0; i < queue_count; ++i) {
                thread* value = queue[i];
                if (value->state() != thread_state::runnable ||
                    value->sched_class() != scheduling_class::realtime) {
                    continue;
                }
                if (realtime_index < 0 || value->rt_priority() > realtime_priority) {
                    realtime_index    = static_cast<i32>(i);
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
            u64 lowest       = ~u64{0};
            for (u32 i = 0; i < queue_count; ++i) {
                thread* value = queue[i];
                if (value->state() != thread_state::runnable) {
                    continue;
                }
                if (normal_index < 0 || value->vruntime() < lowest) {
                    normal_index = static_cast<i32>(i);
                    lowest       = value->vruntime();
                }
            }
            return normal_index < 0 ? nullptr : remove_at(static_cast<u32>(normal_index));
        }

        u64 nice_charge(i32 nice) {
            // Integer approximation of a weighted fair scheduler: nice
            // -20 advances slowly, nice 19 advances quickly.
            return nice < 0 ? 1024u / static_cast<u32>(1 - nice)
                            : 1024u * static_cast<u32>(1 + nice);
        }

        /// Restore the interrupt gate after the scheduler has finished its
        /// masked bookkeeping.  A thread parked on a timer frame stays
        /// masked — its iret restores RFLAGS.IF — while a thread that
        /// blocked in kernel code gets back exactly what it yielded with.
        void restore_gate(bool open) {
            if (open) {
                arch::cpu::interrupts();
            }
        }
    } // namespace

    bool init() {
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

    mm::shptr<thread> create_kernel(kernel_entry entry, void* argument,
                                    scheduling_class sched_class, i32 nice,
                                    u8 rt_priority) {
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

    bool spawn_kernel(kernel_entry entry, void* argument, scheduling_class sched_class,
                      i32 nice, u8 rt_priority) {
        return static_cast<bool>(
            create_kernel(entry, argument, sched_class, nice, rt_priority));
    }

    bool wake(thread* value) {
        if (value == nullptr || value->state() == thread_state::dead) {
            return false;
        }
        value->state(thread_state::runnable);
        return enqueue(value);
    }

    void tick() {
        thread* current = proc::current_thread();
        if (current != nullptr && current->sched_class() == scheduling_class::normal) {
            current->charge_runtime(nice_charge(current->nice()));
        }
        if (queue_count == 0) {
            return;
        }
        // The timer's dispatch has already EOI'd the source.  The gate
        // stays shut across the switch: yield() saves this thread's
        // context and must not be re-entered in the middle of doing it
        // (see the comment there).  The context that resumes is given its
        // own gate by yield(), and a thread parked on this timer frame
        // gets it from the iret that pops the frame.
        yield();
    }

    void yield() {
        if (!initialized) {
            return;
        }
        thread* previous = proc::current_thread();
        if (previous == nullptr) {
            return;
        }

        // Remember the caller's gate before masking it.  The value is kept
        // locally until the switch is committed, so an interrupt in this
        // small entry window cannot overwrite the resume state of a later
        // switch.  Blocking protocols such as waitpid and futex yield
        // masked and therefore resume masked.
        const bool gate_open = arch::cpu::check_interrupts();
        if (gate_open && previous->sched_class() == scheduling_class::normal) {
            previous->charge_runtime(nice_charge(previous->nice()));
        }

        // This body must run to completion.  It saves the live context
        // into `previous` and, on the way back in, hands the resumed
        // thread its gate.  A timer frame landing between the enqueue
        // below and the switch would run all of it a second time on this
        // same stack, and the second save would park `next`'s context on
        // `previous`'s kernel stack — after which two threads share one
        // stack.  So the gate stays shut until the switch has happened.
        arch::cpu::nointerrupts();

        if (!previous->idle() && (previous->state() == thread_state::running ||
                                  previous->state() == thread_state::runnable)) {
            previous->state(thread_state::runnable);
            if (!enqueue(previous)) {
                previous->state(thread_state::running);
                restore_gate(gate_open);
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
            restore_gate(gate_open);
            return;
        }
        if (next == previous && gate_open) {
            // A voluntary yield must hand off when another runnable thread
            // exists, even if vruntime still prefers the caller.  Otherwise
            // a thread charged by a timer tick can starve a fresh thread
            // while repeatedly selecting itself.
            next = select_next();
            if (next == nullptr) {
                previous->state(thread_state::running);
                restore_gate(gate_open);
                return;
            }
        }
        if (next == previous) {
            previous->state(thread_state::running);
            restore_gate(gate_open);
            return;
        }
        next->state(thread_state::running);
        proc::set_current_thread(next);
        if (next->owner()->address_space() != nullptr) {
            next->owner()->address_space()->activate();
        }
        else if (arch::kernel_page_map() != nullptr) {
            arch::kernel_page_map()->activate();
        }
        if (next->kernel_stack_top() != 0) {
            arch::cpu::set_kernel_stack(next->kernel_stack_top());
        }
        previous->gate_open(gate_open);
        arch::context_switch(previous->context(), next->context());
        // We are now running as `next`, still masked.  `previous` (and any
        // thread that deferred its own destruction in finish()) can no
        // longer run on its kernel stack, so dead threads queued by
        // finish() can be destroyed safely here — with the gate still
        // shut, because the heap this frees into is shared with this very
        // path.
        proc::reap_dead_threads();
        restore_gate(next->gate_open());
    }

    bool ready() {
        return initialized;
    }

} // namespace astia::proc::scheduler
