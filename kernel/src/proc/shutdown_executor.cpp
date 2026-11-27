#include "shutdown_executor.hpp"

#include <astia/arch/timer.hpp>
#include <astia/atomic.hpp>

#include "scheduler.hpp"

namespace astia::proc {
    namespace {
        enum class slot_status : u8 {
            free,
            running,
            succeeded,
            failed,
            abandoned,
        };

        struct slot {
            astia::atomic<slot_status> status{slot_status::free};
            shutdown::handler          fn      = nullptr;
            void*                      context = nullptr;
        };

        // One dispatch runs at most handler_capacity handlers, so capacity slots
        // are enough for a dispatch in which every handler times out. Abandoned
        // slots are never reused because their handler may still be running;
        // static storage keeps a late finish safe after the dispatch moved on.
        slot slots[shutdown::detail::handler_capacity]{};
        usz  slots_used = 0;

        void handler_entry(void* argument) {
            auto* task = static_cast<slot*>(argument);

            if (task->status.load() != slot_status::running)
                return;

            const bool ok = task->fn(task->context);

            slot_status expected = slot_status::running;

            (void) task->status.compare_exchange_strong(
                expected, ok ? slot_status::succeeded : slot_status::failed);
        }

        bool start(shutdown::handler fn, void* context, void** task) {
            if (fn == nullptr || task == nullptr ||
                slots_used == shutdown::detail::handler_capacity)
                return false;

            slot& value = slots[slots_used];

            value.fn      = fn;
            value.context = context;

            value.status.store(slot_status::running);

            if (!scheduler::spawn_kernel(handler_entry, &value)) {
                value.fn      = nullptr;
                value.context = nullptr;

                value.status.store(slot_status::free);

                return false;
            }

            *task = &value;

            ++slots_used;

            return true;
        }

        int poll(void* task) {
            switch (static_cast<slot*>(task)->status.load()) {
            case slot_status::succeeded:
                return 1;
            case slot_status::failed:
            case slot_status::abandoned:
                return 2;
            case slot_status::free:
            case slot_status::running:
                break;
            }

            return 0;
        }

        void abandon(void* task) {
            slot_status expected = slot_status::running;

            (void) static_cast<slot*>(task)->status.compare_exchange_strong(
                expected, slot_status::abandoned);
        }

        void relax() {
            scheduler::yield();
        }

        u64 now_ns() {
            const u64 hz = arch::timer_hz();
            if (hz == 0)
                return 0;

            const u64 ticks = arch::timer_ticks();
            if (ticks == 0)
                return 0;

            const u64 seconds   = ticks / hz;
            const u64 remainder = ticks % hz;

            return seconds * nanoseconds_per_second +
                   remainder * nanoseconds_per_second / hz;
        }
    } // namespace

    const shutdown::detail::dispatch_executor shutdown_executor{
        start, poll, abandon, relax, now_ns,
    };
} // namespace astia::proc
