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
    struct fd_t {
        i32 value        = -1;
        constexpr fd_t() = default;
        constexpr explicit fd_t(i32 raw) : value(raw) {}
        constexpr bool operator==(fd_t other) const {
            return value == other.value;
        }
        constexpr bool operator!=(fd_t other) const {
            return value != other.value;
        }
    };

    template <typename Tag>
    struct domain_id {
        u32 value             = 0;
        constexpr domain_id() = default;
        constexpr explicit domain_id(u32 raw) : value(raw) {}
        constexpr explicit operator u32() const {
            return value;
        }
        constexpr bool operator==(domain_id other) const {
            return value == other.value;
        }
        constexpr bool operator!=(domain_id other) const {
            return value != other.value;
        }
    };
    struct uid_tag;
    struct gid_tag;
    struct pid_tag;
    struct pgrp_tag;
    using uid_t  = domain_id<uid_tag>;
    using gid_t  = domain_id<gid_tag>;
    using pid_t  = domain_id<pid_tag>;
    using pgrp_t = domain_id<pgrp_tag>;

    constexpr pid_t pid_from_raw(u32 raw) {
        return pid_t{raw};
    }

    constexpr pgrp_t pgrp_from_raw(u32 raw) {
        return pgrp_t{raw};
    }

    /// Explicitly name the ABI conversion at syscall boundaries.
    template <typename Tag>
    constexpr u32 raw_id(domain_id<Tag> id) {
        return static_cast<u32>(id);
    }

    /// Explicit vocabulary for values that otherwise tend to be naked u64s
    /// or machine-sized integers at subsystem boundaries. These are aliases,
    /// not wrapper types, so they preserve the existing ABI and arithmetic.
    using byte_t   = u8;
    using bytes_t  = usize;
    using count_t  = usize;
    using offset_t = usize;
    using paddr_t  = u64;
    using vaddr_t  = u64;

    inline constexpr usize cache_line_bytes = 64;

    /// Kernel page geometry.  Keep the size and its low-bit mask together so
    /// generic memory code does not grow independent copies of the geometry.
    namespace mm {
        inline constexpr usize page_size = 4096;
        inline constexpr u64   page_mask = page_size - 1;
    } // namespace mm

    /// Time conversion factors used by kernel clocks and deadlines. These are
    /// constants rather than converting duration types so existing raw-count
    /// arithmetic and ABI representations remain unchanged.
    inline constexpr u64 nanoseconds_per_microsecond = 1000ull;
    inline constexpr u64 microseconds_per_second     = 1000000ull;
    inline constexpr u64 nanoseconds_per_second      = 1000000000ull;

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

} // namespace astia
