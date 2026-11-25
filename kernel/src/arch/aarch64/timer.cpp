#include "timer.hpp"

namespace astia::arch::aarch64::timer {
    namespace {
        void isb() {
            __asm__ volatile("isb" : : : "memory");
        }

        [[nodiscard]] u32 read_frequency() {
            u64 value = 0;
            __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(value));
            return static_cast<u32>(value);
        }

        [[nodiscard]] u64 read_count() {
            u64 value = 0;
            __asm__ volatile("mrs %0, cntpct_el0" : "=r"(value));
            return value;
        }

        void write_control(u32 value) {
            __asm__ volatile("msr cntp_ctl_el0, %0" : : "r"(u64{value}) : "memory");
            isb();
        }

        void write_tval(u32 value) {
            __asm__ volatile("msr cntp_tval_el0, %0" : : "r"(u64{value}) : "memory");
            isb();
        }
    } // namespace

    const char* describe(error value) {
        switch (value) {
        case error::none:
            return "none";
        case error::frequency_zero:
            return "frequency-zero";
        case error::interval_unrepresentable:
            return "interval-unrepresentable";
        }
        return "unknown";
    }

    error arm_interval(u32 ticks, u32& control) {
        if (!valid_interval(ticks)) {
            return error::interval_unrepresentable;
        }

        write_control(ctl_disabled_masked);
        write_tval(ticks);
        write_control(ctl_enable);
        control = read_control();
        return error::none;
    }

    error arm_one_shot(arm_result& result) {
        const u32 frequency = read_frequency();
        u32       ticks     = 0;
        if (!one_shot_ticks(frequency, one_shot_interval_ms, ticks)) {
            return frequency == 0 ? error::frequency_zero
                                  : error::interval_unrepresentable;
        }

        u32         control   = 0;
        const error arm_error = arm_interval(ticks, control);
        if (arm_error != error::none) {
            return arm_error;
        }
        result = arm_result{frequency, ticks, control};
        return error::none;
    }

    void disable_and_clear() {
        write_control(ctl_disabled_masked);
        write_tval(tval_max_positive);
    }

    u32 read_control() {
        u64 value = 0;
        __asm__ volatile("mrs %0, cntp_ctl_el0" : "=r"(value));
        return static_cast<u32>(value);
    }

    u64 now_ms() {
        u64 ms = 0;
        if (!count_to_ms(read_count(), read_frequency(), ms)) {
            return 0;
        }
        return ms;
    }

} // namespace astia::arch::aarch64::timer
