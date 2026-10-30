#include "proc.hpp"
#include <astia/api/syscalls.h>
#include <astia/arch/interrupts.hpp>
#include <astia/log.hpp>

namespace astia::log::detail {

    namespace {
        inline constexpr u32 pending_capacity = 4096;
        inline constexpr u32 level_width      = 5;
        char                 pending[2][pending_capacity];
        u32                  pending_count[2]{};

        u32 pending_slot(level severity) {
            return severity == level::info ? 0 : 1;
        }

        void buffer(level severity, char c) {
            const u32 slot = pending_slot(severity);
            if (pending_count[slot] < pending_capacity) {
                pending[slot][pending_count[slot]++] = c;
            }
        }

        const char* level_name(level severity) {
            switch (severity) {
            case level::debug:
                return "DEBUG";
            case level::info:
                return "INFO";
            case level::warning:
                return "WARN";
            case level::error:
                return "ERROR";
            case level::panic:
                return "PANIC";
            }
            return "?";
        }

        /// A number right-aligned in `width` columns, padded on the left
        /// with `pad`.
        void field(level severity, u64 value, u32 width, char pad) {
            char digits[20];
            u32  count = 0;
            do {
                digits[count++] = static_cast<char>('0' + value % 10);
                value /= 10;
            } while (value != 0 && count < sizeof(digits));
            for (u32 i = count; i < width; ++i) {
                put(severity, pad);
            }
            while (count != 0) {
                put(severity, digits[--count]);
            }
        }

        /// Seconds since the timer started, and the fraction of a second the
        /// tick rate actually resolves: `  123.456789`.  Before there is a
        /// timer the clock reads zero rather than refusing to log.
        void time_field(level severity) {
            const u64 hz     = arch::timer_hz();
            const u64 ticks  = arch::timer_ticks();
            const u64 micros = hz == 0 ? 0 : ticks * microseconds_per_second / hz;
            field(severity, micros / microseconds_per_second, 5, ' ');
            put(severity, '.');
            field(severity, micros % microseconds_per_second, 6, '0');
        }

        /// The severity, right-aligned in five columns: ` INFO`, `ERROR`.
        void level_field(level severity) {
            const char* word   = level_name(severity);
            u32         length = 0;
            while (word[length] != '\0') {
                ++length;
            }
            for (u32 i = length; i < level_width; ++i) {
                put(severity, ' ');
            }
            for (u32 i = 0; i < length; ++i) {
                put(severity, word[i]);
            }
        }
    } // namespace

    void put(level severity, char c) {
        proc::process* current = proc::current_process();
        auto           stream =
            current == nullptr
                ? mm::shptr<fs::open_file>{}
                : current->descriptor(fd_t{severity == level::info ? STDOUT : STDERR});
        if (!stream) {
            buffer(severity, c);
            return;
        }

        const u32 slot    = pending_slot(severity);
        u32       flushed = 0;
        while (flushed < pending_count[slot] &&
               stream->write(&pending[slot][flushed], 1, fs::offset_current) == 1) {
            ++flushed;
        }
        if (flushed != pending_count[slot]) {
            u32 remaining = pending_count[slot] - flushed;
            for (u32 i = 0; i < remaining; ++i) {
                pending[slot][i] = pending[slot][flushed + i];
            }
            pending_count[slot] = remaining;
            buffer(severity, c);
            return;
        }
        pending_count[slot] = 0;

        if (stream->write(&c, 1, fs::offset_current) != 1) {
            buffer(severity, c);
        }
    }

    /// The `[time LEVEL] context: ` a line opens with.
    void prefix(level severity, const char* context, u32 length) {
        put(severity, '[');
        time_field(severity);
        put(severity, ' ');
        level_field(severity);
        put(severity, ']');
        put(severity, ' ');
        for (u32 i = 0; i < length; ++i) {
            put(severity, context[i]);
        }
        put(severity, ':');
        put(severity, ' ');
    }

    void number(level severity, u64 value, unsigned base, bool upper) {
        char        buffer[32];
        u32         count  = 0;
        const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
        do {
            buffer[count++] = digits[value % base];
            value /= base;
        } while (value != 0 && count < sizeof(buffer));
        while (count != 0) {
            put(severity, buffer[--count]);
        }
    }

    void signed_number(level severity, i64 value) {
        if (value < 0) {
            put(severity, '-');
            number(severity, static_cast<u64>(-(value + 1)) + 1, 10, false);
        }
        else {
            number(severity, static_cast<u64>(value), 10, false);
        }
    }

} // namespace astia::log::detail
