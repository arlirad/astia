#pragma once

#include <astia/common.hpp>

namespace astia::plat::aarch64_virt {
    class pl011_uart final {
        public:
        explicit constexpr pl011_uart(u64 base) : base_(base) {}

        void write(const char* text) const;
        void write_line(const char* text) const;
        void write_hex(u64 value, u32 digits) const;

        /// Read UARTFR. Used as the post-MMU MMIO read marker.
        u32 read_flags() const;

        private:
        void write_char(char value) const;

        u64 base_;
    };
} // namespace astia::plat::aarch64_virt
