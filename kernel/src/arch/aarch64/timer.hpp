#pragma once

#include <astia/common.hpp>

// Bounded EL1 physical timer and fixed-capacity deadline contract for the
// QEMU virt boot stage: PPI 30, Non-secure Group 1, priority 0xa0,
// level-sensitive, positive TVAL intervals below INT32_MAX.  The PPI drives a
// 100 Hz periodic tick service that counts delivered and missed periods from
// CNTPCT and dispatches an eight-entry deadline queue inside the registered
// PPI 30 route before EOIR/DIR so the level source is deasserted.
// notify_timer_tick() publishes the arch timer hook and is the bounded
// scheduler tick source; no unbounded timer queue, timer wheel, or generic
// timer abstraction exists.
namespace astia::arch::aarch64::timer {
    inline constexpr u32 ppi_id               = 30;
    inline constexpr u32 ctl_enable           = 1u << 0;
    inline constexpr u32 ctl_imask            = 1u << 1;
    inline constexpr u32 ctl_istatus          = 1u << 2;
    inline constexpr u32 ctl_disabled_masked  = ctl_imask;
    inline constexpr u32 tick_interval_ms     = 10;
    inline constexpr u32 tick_hz              = 1000 / tick_interval_ms;
    inline constexpr u32 tval_max_positive    = 0x7fffffffu;
    inline constexpr u32 periodic_tick_budget = 5;
    inline constexpr u32 deadline_capacity    = 8;

    enum class tick_action : u8 {
        rearm,
        stop,
    };

    [[nodiscard]] constexpr tick_action next_tick_action(u32 handled_ticks, u32 budget) {
        if (budget == 0 || handled_ticks >= budget)
            return tick_action::stop;

        return handled_ticks + 1 < budget ? tick_action::rearm : tick_action::stop;
    }

    enum class error : u8 {
        none,
        frequency_zero,
        interval_unrepresentable,
    };

    const char* describe(error value);

    [[nodiscard]] constexpr bool interval_ticks(u32 frequency_hz, u32 interval_ms,
                                                u32& ticks) {
        if (frequency_hz == 0 || interval_ms == 0)
            return false;

        const u64 frequency = frequency_hz;
        const u64 interval  = interval_ms;

        if (frequency > ~u64{0} / interval)
            return false;

        const u64 scaled = frequency * interval;

        if (scaled > ~u64{0} - 999)
            return false;

        const u64 rounded = (scaled + 999) / 1000;

        if (rounded == 0 || rounded > tval_max_positive)
            return false;

        ticks = static_cast<u32>(rounded);

        return true;
    }

    [[nodiscard]] constexpr bool valid_interval(u32 ticks) {
        return ticks != 0 && ticks <= tval_max_positive;
    }

    [[nodiscard]] constexpr u64 saturating_add(u64 value, u64 increment) {
        return value > ~u64{0} - increment ? ~u64{0} : value + increment;
    }

    struct tick_progress {
        u64 delivered = 0;
        u64 missed    = 0;
    };

    // Monotonic timekeeping counters for the PPI tick service.  Delivered
    // ticks count handled PPI interrupts; missed ticks count whole periods
    // that elapsed between two handled interrupts.  tick_count includes both,
    // so arch::timer_ticks()/arch::timer_hz() keep wall time aligned with
    // CNTPCT when a handler is delayed.  All arithmetic saturates.
    struct timekeeping_state {
        u64  delivered_ticks = 0;
        u64  missed_ticks    = 0;
        u64  tick_count      = 0;
        u64  last_tick_ms    = 0;
        bool active          = false;
    };

    inline timekeeping_state timekeeping{};

    inline void reset_timekeeping(u64 start_ms) {
        timekeeping              = timekeeping_state{};
        timekeeping.last_tick_ms = start_ms;
    }

    inline void begin_timekeeping(u64 start_ms) {
        timekeeping.last_tick_ms = start_ms;
        timekeeping.active       = true;
    }

    [[nodiscard]] inline tick_progress account_tick(u64 now_ms) {
        u64 delta = 0;

        if (now_ms >= timekeeping.last_tick_ms)
            delta = (now_ms - timekeeping.last_tick_ms) / tick_interval_ms;

        const u64 missed = delta == 0 ? 0 : delta - 1;

        timekeeping.delivered_ticks = saturating_add(timekeeping.delivered_ticks, 1);
        timekeeping.missed_ticks    = saturating_add(timekeeping.missed_ticks, missed);
        timekeeping.tick_count =
            saturating_add(timekeeping.tick_count, saturating_add(1, missed));

        if (delta != 0) {
            constexpr u64 max_periods = ~u64{0} / tick_interval_ms;
            const u64     periods     = delta > max_periods ? max_periods : delta;

            timekeeping.last_tick_ms =
                saturating_add(timekeeping.last_tick_ms, periods * tick_interval_ms);
        }

        return tick_progress{1, missed};
    }

    [[nodiscard]] inline u64 tick_count() {
        return timekeeping.tick_count;
    }

    [[nodiscard]] inline u64 delivered_ticks() {
        return timekeeping.delivered_ticks;
    }

    [[nodiscard]] inline u64 missed_ticks() {
        return timekeeping.missed_ticks;
    }

    [[nodiscard]] inline bool periodic_active() {
        return timekeeping.active;
    }

    using deadline_handler = void (*)(void* context);

    enum class deadline_error : u8 {
        none,
        full,
        invalid_delay,
        null_handler,
    };

    struct deadline_entry {
        u64              due_ms  = 0;
        deadline_handler handler = nullptr;
        void*            context = nullptr;
        u32              id      = 0;
        bool             armed   = false;
    };

    // Fixed-capacity deadline table.  Callbacks run in IRQ context inside the
    // PPI 30 timer handler before EOIR/DIR; they must not block or enable
    // interrupts.
    inline deadline_entry deadline_table[deadline_capacity]{};
    inline u32            deadline_next_id = 1;

    [[nodiscard]] constexpr bool deadline_due(u64 due_ms, u64 now_ms) {
        return due_ms <= now_ms;
    }

    [[nodiscard]] constexpr bool add_ms_checked(u64 now_ms, u32 delay_ms, u64& due_ms) {
        const u64 delay = delay_ms;

        if (now_ms > ~u64{0} - delay)
            return false;

        due_ms = now_ms + delay;

        return true;
    }

    [[nodiscard]] constexpr u32 earliest_due_index(const deadline_entry* table,
                                                   u32                   count) {
        u32 earliest = ~u32{0};
        u64 due      = 0;

        for (u32 index = 0; index < count; ++index) {
            if (!table[index].armed)
                continue;

            if (earliest == ~u32{0} || table[index].due_ms < due) {
                earliest = index;
                due      = table[index].due_ms;
            }
        }

        return earliest;
    }

    [[nodiscard]] constexpr bool count_to_ms(u64 count, u32 frequency_hz, u64& ms) {
        if (frequency_hz == 0)
            return false;

        const u64 frequency = frequency_hz;
        const u64 whole     = count / frequency;

        if (whole > ~u64{0} / 1000)
            return false;

        const u64 whole_ms = whole * 1000;
        const u64 part_ms  = ((count % frequency) * 1000) / frequency;

        if (whole_ms > ~u64{0} - part_ms)
            return false;

        ms = whole_ms + part_ms;

        return true;
    }

    inline deadline_error schedule_deadline(u64 now_ms, u32 delay_ms,
                                            deadline_handler handler, void* context,
                                            u32& id) {
        if (delay_ms == 0)
            return deadline_error::invalid_delay;

        if (handler == nullptr)
            return deadline_error::null_handler;

        u64 due_ms = 0;

        if (!add_ms_checked(now_ms, delay_ms, due_ms))
            return deadline_error::invalid_delay;

        u32 slot = deadline_capacity;

        for (u32 index = 0; index < deadline_capacity; ++index) {
            if (!deadline_table[index].armed) {
                slot = index;

                break;
            }
        }

        if (slot == deadline_capacity)
            return deadline_error::full;

        // An assigned id never collides with a currently armed entry.
        u32 candidate = deadline_next_id == 0 ? 1 : deadline_next_id;

        for (u32 attempt = 0; attempt <= deadline_capacity; ++attempt) {
            bool taken = false;

            for (u32 index = 0; index < deadline_capacity; ++index) {
                if (deadline_table[index].armed &&
                    deadline_table[index].id == candidate) {
                    taken = true;

                    break;
                }
            }

            if (!taken)
                break;

            candidate = candidate == ~u32{0} ? 1 : candidate + 1;
        }

        id                   = candidate;
        deadline_next_id     = candidate == ~u32{0} ? 1 : candidate + 1;
        deadline_table[slot] = deadline_entry{due_ms, handler, context, id, true};

        return deadline_error::none;
    }

    inline bool cancel_deadline(u32 id) {
        for (u32 index = 0; index < deadline_capacity; ++index) {
            if (deadline_table[index].armed && deadline_table[index].id == id) {
                deadline_table[index] = deadline_entry{};

                return true;
            }
        }

        return false;
    }

    inline u32 dispatch_due(u64 now_ms) {
        u32 fired = 0;

        while (true) {
            const u32 index = earliest_due_index(deadline_table, deadline_capacity);

            if (index == ~u32{0})
                break;

            deadline_entry& entry = deadline_table[index];

            if (!deadline_due(entry.due_ms, now_ms))
                break;

            const deadline_handler handler = entry.handler;
            void* const            context = entry.context;

            entry = deadline_entry{};

            ++fired;

            if (handler != nullptr)
                handler(context);
        }

        return fired;
    }

    inline u32 pending_deadlines() {
        u32 pending = 0;

        for (u32 index = 0; index < deadline_capacity; ++index) {
            if (deadline_table[index].armed)
                ++pending;
        }

        return pending;
    }

    inline void reset_deadlines() {
        for (u32 index = 0; index < deadline_capacity; ++index) {
            deadline_table[index] = deadline_entry{};
        }

        deadline_next_id = 1;
    }

    u64 now_ms();

    struct periodic_result {
        u32 frequency_hz = 0;
        u32 ticks        = 0;
        u32 control      = 0;
        u64 start_ms     = 0;
    };

    error start_periodic(periodic_result& result);
    void  stop_periodic();
    error arm_interval(u32 ticks, u32& control);
    void  disable_and_clear();
    u32   read_control();
} // namespace astia::arch::aarch64::timer
