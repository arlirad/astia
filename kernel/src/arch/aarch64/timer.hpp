#pragma once

#include <astia/common.hpp>

// Bounded EL1 physical timer and fixed-capacity deadline contract for the
// QEMU virt boot stage: PPI 30, Non-secure Group 1, priority 0xa0,
// level-sensitive, positive TVAL intervals below INT32_MAX.  The boot proof
// re-arms a bounded periodic tick and dispatches an eight-entry deadline queue
// inside the registered handler before EOIR/DIR so the level source is
// deasserted; no scheduler, unbounded timer queue, or generic abstraction
// exists.
namespace astia::arch::aarch64::timer {

    inline constexpr u32 ppi_id               = 30;
    inline constexpr u32 ctl_enable           = 1u << 0;
    inline constexpr u32 ctl_imask            = 1u << 1;
    inline constexpr u32 ctl_istatus          = 1u << 2;
    inline constexpr u32 ctl_disabled_masked  = ctl_imask;
    inline constexpr u32 one_shot_interval_ms = 10;
    inline constexpr u32 tval_max_positive    = 0x7fffffffu;
    inline constexpr u32 periodic_tick_budget = 5;
    inline constexpr u32 deadline_capacity    = 8;

    enum class tick_action : u8 {
        rearm,
        stop,
    };

    [[nodiscard]] constexpr tick_action next_tick_action(u32 handled_ticks, u32 budget) {
        if (budget == 0 || handled_ticks >= budget) {
            return tick_action::stop;
        }
        return handled_ticks + 1 < budget ? tick_action::rearm : tick_action::stop;
    }

    enum class error : u8 {
        none,
        frequency_zero,
        interval_unrepresentable,
    };

    const char* describe(error value);

    [[nodiscard]] constexpr bool one_shot_ticks(u32 frequency_hz, u32 interval_ms,
                                                u32& ticks) {
        if (frequency_hz == 0 || interval_ms == 0) {
            return false;
        }
        const u64 frequency = frequency_hz;
        const u64 interval  = interval_ms;
        if (frequency > ~u64{0} / interval) {
            return false;
        }
        const u64 scaled = frequency * interval;
        if (scaled > ~u64{0} - 999) {
            return false;
        }
        const u64 rounded = (scaled + 999) / 1000;
        if (rounded == 0 || rounded > tval_max_positive) {
            return false;
        }
        ticks = static_cast<u32>(rounded);
        return true;
    }

    [[nodiscard]] constexpr bool valid_interval(u32 ticks) {
        return ticks != 0 && ticks <= tval_max_positive;
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
        if (now_ms > ~u64{0} - delay) {
            return false;
        }
        due_ms = now_ms + delay;
        return true;
    }

    [[nodiscard]] constexpr u32 earliest_due_index(const deadline_entry* table,
                                                   u32                   count) {
        u32 earliest = ~u32{0};
        u64 due      = 0;
        for (u32 index = 0; index < count; ++index) {
            if (!table[index].armed) {
                continue;
            }
            if (earliest == ~u32{0} || table[index].due_ms < due) {
                earliest = index;
                due      = table[index].due_ms;
            }
        }
        return earliest;
    }

    [[nodiscard]] constexpr bool count_to_ms(u64 count, u32 frequency_hz, u64& ms) {
        if (frequency_hz == 0 || count > ~u64{0} / 1000) {
            return false;
        }
        ms = (count * 1000) / frequency_hz;
        return true;
    }

    inline deadline_error schedule_deadline(u64 now_ms, u32 delay_ms,
                                            deadline_handler handler, void* context,
                                            u32& id) {
        if (delay_ms == 0) {
            return deadline_error::invalid_delay;
        }
        if (handler == nullptr) {
            return deadline_error::null_handler;
        }
        u64 due_ms = 0;
        if (!add_ms_checked(now_ms, delay_ms, due_ms)) {
            return deadline_error::invalid_delay;
        }
        u32 slot = deadline_capacity;
        for (u32 index = 0; index < deadline_capacity; ++index) {
            if (!deadline_table[index].armed) {
                slot = index;
                break;
            }
        }
        if (slot == deadline_capacity) {
            return deadline_error::full;
        }
        id = deadline_next_id;
        ++deadline_next_id;
        if (deadline_next_id == 0) {
            deadline_next_id = 1;
        }
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
            if (index == ~u32{0}) {
                break;
            }
            deadline_entry& entry = deadline_table[index];
            if (!deadline_due(entry.due_ms, now_ms)) {
                break;
            }
            const deadline_handler handler = entry.handler;
            void* const            context = entry.context;
            entry                          = deadline_entry{};
            ++fired;
            if (handler != nullptr) {
                handler(context);
            }
        }
        return fired;
    }

    inline u32 pending_deadlines() {
        u32 pending = 0;
        for (u32 index = 0; index < deadline_capacity; ++index) {
            if (deadline_table[index].armed) {
                ++pending;
            }
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

    struct arm_result {
        u32 frequency_hz = 0;
        u32 ticks        = 0;
        u32 control      = 0;
    };

    error arm_one_shot(arm_result& result);
    error arm_interval(u32 ticks, u32& control);
    void  disable_and_clear();
    u32   read_control();

} // namespace astia::arch::aarch64::timer
