#pragma once

#include <astia/common.hpp>

#include <type_traits>

namespace astia {

    /// Minimal freestanding atomic over the compiler's `__atomic` builtins.
    /// The kernel does not use libstdc++'s `<atomic>` (it drags hosted
    /// assertions into the link), so this is the shared primitive for integer
    /// and enum values that the target can access atomically.
    template <typename T>
    class atomic {
        using storage =
            std::conditional_t<std::is_enum_v<T>, std::underlying_type_t<T>, T>;

        static_assert(std::is_integral_v<T> || std::is_enum_v<T>,
                      "astia::atomic covers integral and enum types");
        static_assert(sizeof(storage) <= sizeof(u64),
                      "astia::atomic covers at most 64-bit values");

        storage value_{};

        public:
        constexpr atomic() = default;
        constexpr explicit atomic(T value) : value_(static_cast<storage>(value)) {}

        atomic(const atomic&)            = delete;
        atomic& operator=(const atomic&) = delete;

        T load() const {
            return static_cast<T>(__atomic_load_n(&value_, __ATOMIC_ACQUIRE));
        }

        void store(T value) {
            __atomic_store_n(&value_, static_cast<storage>(value), __ATOMIC_RELEASE);
        }

        bool compare_exchange_strong(T& expected, T desired) {
            storage    observed  = static_cast<storage>(expected);
            const bool exchanged = __atomic_compare_exchange_n(
                &value_, &observed, static_cast<storage>(desired), false,
                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
            expected = static_cast<T>(observed);
            return exchanged;
        }
    };

} // namespace astia
