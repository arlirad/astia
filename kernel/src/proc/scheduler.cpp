#include "scheduler.hpp"
#include "../ipc/futex.hpp"
#include <astia/core/rcu_runtime.hpp>
#include <astia/core/resume_queue.hpp>

#include "proc.hpp"
#include "../sys/aio.hpp"
#include <astia/arch/context.hpp>
#include <astia/arch/cpu.hpp>
#include <astia/arch/interrupts.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/mm/share.hpp>

namespace astia::proc::scheduler {

    namespace {
        struct cpu_state {
            core::spinlock lock;
            thread*        queue[run_queue_capacity]{};
            u32            queue_count                           = 0;
            thread*        idle_thread                           = nullptr;
            enum class lifecycle : u8 { offline, online } status = lifecycle::offline;
        } cpus[max_cpu_count];
        bool initialized = false;

        void idle_entry(void*) {
            // Entered by a switch like any other thread, and the switcher's
            // gate is not ours to keep: open it so the next tick can take
            // the CPU back out of the halt below.
            arch::cpu::interrupts();
            for (;;) {
                arch::cpu::halt();
                if (arch::cpu::consume_reschedule()) {
                    yield();
                }
            }
        }

        bool enqueue(thread* value) {
            if (value == nullptr) {
                return false;
            }
            const u32 index = value->cpu_owner();
            if (index >= max_cpu_count ||
                cpus[index].status != cpu_state::lifecycle::online)
                return false;
            auto&                          state = cpus[index];
            arch::interrupt_spinlock_guard guard(state.lock);
            if (value->queued()) {
                return true;
            }
            if (state.queue_count == run_queue_capacity) {
                return false;
            }
            value->queued(true);
            state.queue[state.queue_count++] = value;
            return true;
        }

        bool rebalance_idle_cpu(u32 target_index) {
            if (target_index >= max_cpu_count ||
                cpus[target_index].status != cpu_state::lifecycle::online)
                return false;
            for (u32 source_index = 0; source_index < max_cpu_count; ++source_index) {
                if (source_index == target_index ||
                    cpus[source_index].status != cpu_state::lifecycle::online)
                    continue;
                const u32 first =
                    source_index < target_index ? source_index : target_index;
                const u32 second =
                    source_index < target_index ? target_index : source_index;
                arch::interrupt_spinlock_guard first_guard(cpus[first].lock);
                arch::interrupt_spinlock_guard second_guard(cpus[second].lock);
                auto&                          source = cpus[source_index];
                auto&                          target = cpus[target_index];
                if (source.queue_count <=
                        target.queue_count + run_queue_rebalance_slack ||
                    target.queue_count == run_queue_capacity)
                    continue;
                u32 position = source.queue_count;
                for (u32 i = 0; i < source.queue_count; ++i) {
                    if (source.queue[i]->state() == thread_state::runnable &&
                        source.queue[i]->migratable()) {
                        position = i;
                        break;
                    }
                }
                if (position == source.queue_count)
                    continue;
                thread* value = source.queue[position];
                for (u32 i = position + 1; i < source.queue_count; ++i)
                    source.queue[i - 1] = source.queue[i];
                --source.queue_count;
                value->cpu_owner(target_index);
                target.queue[target.queue_count++] = value;
                return true;
            }
            return false;
        }

        void remove_from_queue(cpu_state& state, u32 position) {
            thread* value = state.queue[position];
            value->queued(false);
            for (u32 i = position + 1; i < state.queue_count; ++i)
                state.queue[i - 1] = state.queue[i];
            --state.queue_count;
        }

        thread* select_next(u32 index) {
            auto&                          state = cpus[index];
            arch::interrupt_spinlock_guard guard(state.lock);
            if (state.queue_count == 0) {
                return nullptr;
            }

            // Realtime policy: strict priority, FIFO among equal priority.
            i32 realtime_index    = -1;
            u8  realtime_priority = 0;
            for (u32 i = 0; i < state.queue_count; ++i) {
                thread* value = state.queue[i];
                if (value->state() != thread_state::runnable ||
                    value->owner()->state() == process_state::stopped ||
                    value->sched_class() != scheduling_class::realtime) {
                    continue;
                }
                if (realtime_index < 0 || value->rt_priority() > realtime_priority) {
                    realtime_index    = static_cast<i32>(i);
                    realtime_priority = value->rt_priority();
                }
            }
            if (realtime_index >= 0) {
                thread* value = state.queue[realtime_index];
                remove_from_queue(state, static_cast<u32>(realtime_index));
                return value;
            }

            // Normal policy: lowest virtual runtime first.  nice affects
            // the charge applied on each timer tick, so a lower nice value
            // advances more slowly and receives more CPU time.
            i32 normal_index = -1;
            u64 lowest       = ~u64{0};
            for (u32 i = 0; i < state.queue_count; ++i) {
                thread* value = state.queue[i];
                if (value->state() != thread_state::runnable ||
                    value->owner()->state() == process_state::stopped) {
                    continue;
                }
                if (normal_index < 0 || value->vruntime() < lowest) {
                    normal_index = static_cast<i32>(i);
                    lowest       = value->vruntime();
                }
            }
            if (normal_index < 0)
                return nullptr;
            thread* value = state.queue[normal_index];
            remove_from_queue(state, static_cast<u32>(normal_index));
            return value;
        }

        u64 nice_charge(i32 nice) {
            // Integer approximation of a weighted fair scheduler: nice
            // -20 advances slowly, nice 19 advances quickly.
            return nice < 0 ? nice_charge_scale / static_cast<u32>(1 - nice)
                            : nice_charge_scale * static_cast<u32>(1 + nice);
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
        if (!init_cpu(arch::cpu::current_index()))
            return false;
        return true;
    }

    bool init_cpu(u32 cpu_index) {
        if (!initialized || cpu_index >= max_cpu_count ||
            cpus[cpu_index].status == cpu_state::lifecycle::online)
            return false;
        auto idle = mm::share<thread>(proc::kernel_process(), proc::allocate_tid());
        if (!idle || !idle->prepare_kernel(idle_entry, nullptr) ||
            !proc::kernel_process()->add_thread(idle)) {
            return false;
        }
        idle->idle(true);
        idle->state(thread_state::runnable);
        idle->cpu_owner(cpu_index);
        cpus[cpu_index].idle_thread = idle.get();
        cpus[cpu_index].status      = cpu_state::lifecycle::online;
        // APs enter directly in their idle loop rather than through a
        // context switch, so publish the CPU-local current object now.  The
        // BSP already owns its bootstrap thread and keeps it.
        if (proc::current_thread() == nullptr) {
            idle->state(thread_state::running);
            proc::set_current_thread(idle.get());
        }
        return true;
    }

    bool start_cpu(u32 cpu_index) {
        if (!initialized || cpu_index >= max_cpu_count ||
            cpus[cpu_index].status != cpu_state::lifecycle::online ||
            proc::current_thread() != nullptr) {
            return false;
        }
        auto* idle = cpus[cpu_index].idle_thread;
        idle->state(thread_state::running);
        proc::set_current_thread(idle);
        yield();
        return true;
    }

    namespace {
        mm::shptr<thread> create_kernel_impl(u32 cpu_index, kernel_entry entry,
                                             void* argument, scheduling_class sched_class,
                                             i32 nice, u8 rt_priority,
                                             thread_options options) {
            if (!initialized || entry == nullptr || proc::kernel_process() == nullptr) {
                return {};
            }
            if (cpu_index >= max_cpu_count ||
                cpus[cpu_index].status != cpu_state::lifecycle::online)
                return {};
            auto value = mm::share<thread>(proc::kernel_process(), proc::allocate_tid());
            if (!value || !value->prepare_kernel(entry, argument) ||
                !proc::kernel_process()->add_thread(value)) {
                return {};
            }
            value->sched_class(sched_class);
            value->nice(nice);
            value->rt_priority(rt_priority);
            value->state(thread_state::runnable);
            value->cpu_owner(cpu_index);
            value->migratable(options == thread_options::migratable);
            arch::interrupt_guard gate;
            if (!enqueue(value.get())) {
                proc::kernel_process()->release_thread(value.get());
                return {};
            }
            if (cpu_index != arch::cpu::current_index()) {
                arch::cpu::request_reschedule();
            }
            return value;
        }

    }

    mm::shptr<thread> create_kernel(kernel_entry entry, void* argument,
                                    scheduling_class sched_class, i32 nice,
                                    u8 rt_priority, thread_options options) {
        return create_kernel_impl(arch::cpu::current_index(), entry, argument,
                                  sched_class, nice, rt_priority, options);
    }

    mm::shptr<thread> create_kernel_on(u32 cpu_index, kernel_entry entry, void* argument,
                                       scheduling_class sched_class, i32 nice,
                                       u8 rt_priority, thread_options options) {
        return create_kernel_impl(cpu_index, entry, argument, sched_class, nice,
                                  rt_priority, options);
    }

    bool spawn_kernel(kernel_entry entry, void* argument, scheduling_class sched_class,
                      i32 nice, u8 rt_priority, thread_options options) {
        auto value =
            create_kernel(entry, argument, sched_class, nice, rt_priority, options);
        if (!value || value->detach() != 0) {
            return false;
        }
        return true;
    }

    bool migrate(thread* value, u32 cpu_index) {
        if (!initialized || value == nullptr || cpu_index >= max_cpu_count ||
            cpus[cpu_index].status != cpu_state::lifecycle::online ||
            value->cpu_owner() == cpu_index || !value->queued() ||
            value->state() != thread_state::runnable) {
            return false;
        }
        const u32 source_index = value->cpu_owner();
        if (source_index >= max_cpu_count ||
            cpus[source_index].status != cpu_state::lifecycle::online)
            return false;

        arch::interrupt_guard gate;
        auto&                 source = cpus[source_index];
        auto&                 target = cpus[cpu_index];
        const u32 first_index  = source_index < cpu_index ? source_index : cpu_index;
        const u32 second_index = source_index < cpu_index ? cpu_index : source_index;
        arch::interrupt_spinlock_guard first_guard(cpus[first_index].lock);
        arch::interrupt_spinlock_guard second_guard(cpus[second_index].lock);
        {
            u32 position = source.queue_count;
            for (u32 i = 0; i < source.queue_count; ++i) {
                if (source.queue[i] == value) {
                    position = i;
                    break;
                }
            }
            if (position == source.queue_count)
                return false;
            remove_from_queue(source, position);
        }

        if (target.queue_count == run_queue_capacity) {
            // Both queues are held in index order, so restoring the source
            // cannot deadlock with a reverse migration on another CPU.
            value->cpu_owner(source_index);
            value->queued(true);
            source.queue[source.queue_count++] = value;
            return false;
        }
        value->cpu_owner(cpu_index);
        value->queued(true);
        target.queue[target.queue_count++] = value;
        arch::cpu::request_reschedule();
        return true;
    }

    bool wake(thread* value) {
        if (value == nullptr || value->state() == thread_state::dead) {
            return false;
        }
        arch::interrupt_guard gate;
        value->state(thread_state::runnable);
        const bool queued = enqueue(value);
        if (queued)
            arch::cpu::request_reschedule();
        return queued;
    }

    void tick() {
        sys::io_poll_tick();
        futex_tick();
        proc::expire_signal_waiters();
        proc::expire_alarms();
        thread* current = proc::current_thread();
        if (current != nullptr && current->sched_class() == scheduling_class::normal) {
            current->charge_runtime(nice_charge(current->nice()));
        }
        // The timer handler must not switch stacks from inside IRQ dispatch.
        // The ISR epilogue consumes this request after dispatch has returned.
        arch::cpu::request_local_reschedule();
    }

    void yield() {
        if (!initialized) {
            return;
        }
        thread*   previous  = proc::current_thread();
        const u32 cpu_index = arch::cpu::current_index();
        if (previous == nullptr) {
            return;
        }

        arch::interrupt_guard gate;
        const bool            gate_open = gate.was_open();
        if (gate_open && previous->sched_class() == scheduling_class::normal) {
            previous->charge_runtime(nice_charge(previous->nice()));
        }

        if (!previous->idle() && (previous->state() == thread_state::running ||
                                  previous->state() == thread_state::runnable)) {
            previous->state(thread_state::runnable);
            if (!enqueue(previous)) {
                previous->state(thread_state::running);
                return;
            }
        }

        thread* next = select_next(cpu_index);
        if (next == nullptr && previous->idle() && rebalance_idle_cpu(cpu_index)) {
            next = select_next(cpu_index);
        }
        if (next == nullptr) {
            next = cpus[cpu_index].idle_thread;
        }
        if (next == nullptr) {
            if (previous->state() != thread_state::dead) {
                previous->state(thread_state::running);
            }
            return;
        }
        if (next == previous && gate_open) {
            // A voluntary yield must hand off when another runnable thread
            // exists, even if vruntime still prefers the caller.  Otherwise
            // a thread charged by a timer tick can starve a fresh thread
            // while repeatedly selecting itself.
            next = select_next(cpu_index);
            if (next == nullptr) {
                previous->state(thread_state::running);
                return;
            }
            enqueue(previous);
        }
        if (next == previous) {
            previous->state(thread_state::running);
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
        gate.release_masked();
        arch::context_switch(previous->context(), next->context());
        if (auto* current = proc::current_thread(); current != nullptr) {
            current->sample_kernel_stack();
        }
        core::system_rcu_quiescent(cpu_index);
        (void) core::system_rcu_poll();
        (void) core::drain_resumes();
        // We are now running as `next`, still masked.  `previous` (and any
        // thread that deferred its own destruction in finish()) can no
        // longer run on its kernel stack, so dead threads queued by
        // finish() can be destroyed safely here — with the gate still
        // shut, because the heap this frees into is shared with this very
        // path.
        proc::reap_dead_threads();
        restore_gate(previous->gate_open());
    }

    bool ready() {
        return initialized;
    }

    extern "C" void astia_reschedule_if_needed() {
        if (!initialized ||
            (!arch::cpu::consume_should_yield() && !arch::cpu::consume_reschedule())) {
            return;
        }
        yield();
    }

} // namespace astia::proc::scheduler
