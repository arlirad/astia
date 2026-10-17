#pragma once

namespace astia {

    /// Common type definitions and constants
    using u8  = unsigned char;
    using u16 = unsigned short;
    using u32 = unsigned int;
    using u64 = unsigned long long;

    using i8  = signed char;
    using i16 = short;
    using i32 = int;
    using i64 = long long;

    using usize = __SIZE_TYPE__;
    using isize = __PTRDIFF_TYPE__;

    /// Explicit vocabulary for values that otherwise tend to be naked u64s
    /// or machine-sized integers at subsystem boundaries. These are aliases,
    /// not wrapper types, so they preserve the existing ABI and arithmetic.
    using byte_t   = u8;
    using bytes_t  = usize;
    using count_t  = usize;
    using offset_t = usize;
    using paddr_t  = u64;
    using vaddr_t  = u64;

    namespace literals {

        consteval bytes_t operator""_b(unsigned long long value) {
            return static_cast<bytes_t>(value);
        }

        consteval bytes_t operator""_kib(unsigned long long value) {
            return static_cast<bytes_t>(value * 1024ull);
        }

        consteval bytes_t operator""_mib(unsigned long long value) {
            return static_cast<bytes_t>(value * 1024ull * 1024ull);
        }

        consteval bytes_t operator""_gib(unsigned long long value) {
            return static_cast<bytes_t>(value * 1024ull * 1024ull * 1024ull);
        }

    } // namespace literals

} // namespace astia
