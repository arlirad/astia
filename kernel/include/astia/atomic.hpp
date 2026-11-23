#pragma once

#include <astia/common.hpp>

#include <type_traits>

namespace astia {

    enum class memory_order : u8 {
        relaxed,
        acquire,
        release,
        acq_rel,
        seq_cst,
    };

    namespace detail {
        constexpr int atomic_order(memory_order order) {
            switch (order) {
            case memory_order::relaxed:
                return __ATOMIC_RELAXED;
            case memory_order::acquire:
                return __ATOMIC_ACQUIRE;
            case memory_order::release:
                return __ATOMIC_RELEASE;
            case memory_order::acq_rel:
                return __ATOMIC_ACQ_REL;
            case memory_order::seq_cst:
                return __ATOMIC_SEQ_CST;
            }
            return __ATOMIC_SEQ_CST;
        }

        template <typename T, bool = std::is_enum_v<T>>
        struct atomic_storage_of {
            using type = T;
        };

        template <typename T>
        struct atomic_storage_of<T, true> {
            using type = std::underlying_type_t<T>;
        };

        template <typename T>
        using atomic_storage = typename atomic_storage_of<T>::type;
    } // namespace detail

    /// Compiler fence with the same order vocabulary as the atomic wrappers.
    inline void atomic_thread_fence(memory_order order) noexcept {
        __atomic_thread_fence(detail::atomic_order(order));
    }

    /// Minimal freestanding atomic over the compiler's `__atomic` builtins.
    /// The kernel does not use libstdc++'s `<atomic>` (it drags hosted
    /// assertions into the link), so this is the shared primitive for integer
    /// and enum values that the target can access atomically.
    template <typename T>
    class atomic {
        using storage = detail::atomic_storage<T>;

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

        T load(memory_order order = memory_order::acquire) const {
            return static_cast<T>(__atomic_load_n(&value_, detail::atomic_order(order)));
        }

        void store(T value, memory_order order = memory_order::release) {
            __atomic_store_n(&value_, static_cast<storage>(value),
                             detail::atomic_order(order));
        }

        T exchange(T value, memory_order order = memory_order::acq_rel) {
            return static_cast<T>(__atomic_exchange_n(
                &value_, static_cast<storage>(value), detail::atomic_order(order)));
        }

        T fetch_add(T delta, memory_order order = memory_order::acq_rel) {
            return static_cast<T>(__atomic_fetch_add(&value_, static_cast<storage>(delta),
                                                     detail::atomic_order(order)));
        }

        T fetch_sub(T delta, memory_order order = memory_order::acq_rel) {
            return static_cast<T>(__atomic_fetch_sub(&value_, static_cast<storage>(delta),
                                                     detail::atomic_order(order)));
        }

        T fetch_or(T mask, memory_order order = memory_order::acq_rel) {
            return static_cast<T>(__atomic_fetch_or(&value_, static_cast<storage>(mask),
                                                    detail::atomic_order(order)));
        }

        T fetch_and(T mask, memory_order order = memory_order::acq_rel) {
            return static_cast<T>(__atomic_fetch_and(&value_, static_cast<storage>(mask),
                                                     detail::atomic_order(order)));
        }

        bool compare_exchange_strong(T& expected, T desired,
                                     memory_order success = memory_order::acq_rel,
                                     memory_order failure = memory_order::acquire) {
            storage    observed  = static_cast<storage>(expected);
            const bool exchanged = __atomic_compare_exchange_n(
                &value_, &observed, static_cast<storage>(desired), false,
                detail::atomic_order(success), detail::atomic_order(failure));
            expected = static_cast<T>(observed);
            return exchanged;
        }
    };

    /// The same operations on an object owned elsewhere (ABI layouts and
    /// otherwise plain fields), so the layout stays untouched.
    template <typename T>
    class atomic_ref {
        using storage = detail::atomic_storage<T>;

        static_assert(std::is_integral_v<T> || std::is_enum_v<T>,
                      "astia::atomic_ref covers integral and enum types");
        static_assert(sizeof(storage) <= sizeof(u64),
                      "astia::atomic_ref covers at most 64-bit values");

        storage* raw() const noexcept {
            return reinterpret_cast<storage*>(value_);
        }

        public:
        explicit atomic_ref(T& value) noexcept : value_(&value) {}

        atomic_ref(const atomic_ref&)            = default;
        atomic_ref& operator=(const atomic_ref&) = default;

        T load(memory_order order = memory_order::acquire) const {
            return static_cast<T>(__atomic_load_n(raw(), detail::atomic_order(order)));
        }

        void store(T value, memory_order order = memory_order::release) {
            __atomic_store_n(raw(), static_cast<storage>(value),
                             detail::atomic_order(order));
        }

        T exchange(T value, memory_order order = memory_order::acq_rel) {
            return static_cast<T>(__atomic_exchange_n(raw(), static_cast<storage>(value),
                                                      detail::atomic_order(order)));
        }

        T fetch_add(T delta, memory_order order = memory_order::acq_rel) {
            return static_cast<T>(__atomic_fetch_add(raw(), static_cast<storage>(delta),
                                                     detail::atomic_order(order)));
        }

        T fetch_sub(T delta, memory_order order = memory_order::acq_rel) {
            return static_cast<T>(__atomic_fetch_sub(raw(), static_cast<storage>(delta),
                                                     detail::atomic_order(order)));
        }

        T fetch_or(T mask, memory_order order = memory_order::acq_rel) {
            return static_cast<T>(__atomic_fetch_or(raw(), static_cast<storage>(mask),
                                                    detail::atomic_order(order)));
        }

        T fetch_and(T mask, memory_order order = memory_order::acq_rel) {
            return static_cast<T>(__atomic_fetch_and(raw(), static_cast<storage>(mask),
                                                     detail::atomic_order(order)));
        }

        bool compare_exchange_strong(T& expected, T desired,
                                     memory_order success = memory_order::acq_rel,
                                     memory_order failure = memory_order::acquire) {
            storage    observed  = static_cast<storage>(expected);
            const bool exchanged = __atomic_compare_exchange_n(
                raw(), &observed, static_cast<storage>(desired), false,
                detail::atomic_order(success), detail::atomic_order(failure));
            expected = static_cast<T>(observed);
            return exchanged;
        }

        private:
        T* value_;
    };

} // namespace astia
