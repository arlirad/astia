#pragma once

#include "types.hpp"

namespace astia::smoke {

    class pl011_uart final {
        public:
        explicit constexpr pl011_uart(u64 base) : base_(base) {}

        void write(const char* text) const;
        void write_line(const char* text) const;
        void write_hex(u64 value, u32 digits) const;

        private:
        void write_char(char value) const;

        u64 base_;
    };

} // namespace astia::smoke
