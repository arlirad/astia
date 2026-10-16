#include <astia/api/syscalls.h>
#include "log.hpp"
#include "proc.hpp"

namespace astia::log::detail {

    namespace {
        inline constexpr u32 pending_capacity = 4096;
        char                 pending[2][pending_capacity];
        u32                  pending_count[2]{};

        auto pending_slot(level severity) -> u32 {
            return severity == level::info ? 0 : 1;
        }

        auto buffer(level severity, char c) -> void {
            const u32 slot = pending_slot(severity);
            if (pending_count[slot] < pending_capacity) {
                pending[slot][pending_count[slot]++] = c;
            }
        }
    } // namespace

    auto put(level severity, char c) -> void {
        proc::process* current = proc::current_process();
        auto           stream =
            current == nullptr
                          ? mm::shptr<fs::open_file>{}
                          : current->descriptor(severity == level::info ? STDOUT : STDERR);
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

    auto prefix(level severity) -> void {
        switch (severity) {
        case level::debug: {
            const char* value = "astia: debug: ";
            while (*value != '\0')
                put(severity, *value++);
            break;
        }
        case level::info: {
            const char* value = "astia: ";
            while (*value != '\0')
                put(severity, *value++);
            break;
        }
        case level::warning: {
            const char* value = "astia: warning: ";
            while (*value != '\0')
                put(severity, *value++);
            break;
        }
        case level::error: {
            const char* value = "astia: error: ";
            while (*value != '\0')
                put(severity, *value++);
            break;
        }
        case level::panic: {
            const char* value = "astia: KERNEL PANIC: ";
            while (*value != '\0')
                put(severity, *value++);
            break;
        }
        }
    }

    auto number(level severity, u64 value, unsigned base, bool upper) -> void {
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

    auto signed_number(level severity, i64 value) -> void {
        if (value < 0) {
            put(severity, '-');
            number(severity, static_cast<u64>(-(value + 1)) + 1, 10, false);
        }
        else {
            number(severity, static_cast<u64>(value), 10, false);
        }
    }

} // namespace astia::log::detail
