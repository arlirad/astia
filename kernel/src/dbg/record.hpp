#pragma once

#include <astia/common.hpp>

namespace astia::dbg {
    /// One bounded `kpanic:` record built on the stack.  The panic path never
    /// allocates and never silently truncates a record: callers split long
    /// register sets across lines.
    struct record {
        static constexpr u32 capacity = 512;

        char text[capacity]{};
        u32  length = 0;

        void clear() {
            length = 0;
        }

        void add(char value) {
            if (length + 1 < capacity)
                text[length++] = value;
        }

        void add(const char* value) {
            for (u32 i = 0; value != nullptr && value[i] != '\0'; ++i) {
                add(value[i]);
            }
        }

        void add_dec(u64 value) {
            if (value == 0) {
                add('0');

                return;
            }

            char digits[20];
            u32  count = 0;

            while (value != 0 && count < sizeof digits) {
                digits[count++] = static_cast<char>('0' + value % 10);
                value /= 10;
            }

            while (count != 0) {
                add(digits[--count]);
            }
        }

        void add_hex(u64 value) {
            add("0x");

            if (value == 0) {
                add('0');

                return;
            }

            char digits[16];
            u32  count = 0;

            while (value != 0 && count < sizeof digits) {
                const u32 digit = static_cast<u32>(value & 0xfu);

                digits[count++] =
                    static_cast<char>(digit < 10 ? '0' + digit : 'a' + digit - 10);
                value >>= 4;
            }

            while (count != 0) {
                add(digits[--count]);
            }
        }
    };
} // namespace astia::dbg
