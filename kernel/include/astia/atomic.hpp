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

        template <typename T>
        struct atomic_traits {
            static constexpr bool supported =
                std::is_integral_v<T> || std::is_enum_v<T> || std::is_pointer_v<T>;
            static constexpr bool arithmetic = std::is_integral_v<T> || std::is_enum_v<T>;
        };

        /// Shared builtin calls over a storage object. Both owning and
        /// referencing wrappers delegate here so their semantics cannot drift.
        template <typename T>
        class atomic_access {
            using storage = atomic_storage<T>;

            public:
            static T load(const storage* value, memory_order order) {
                return static_cast<T>(__atomic_load_n(value, atomic_order(order)));
            }

            static void store(storage* value, T desired, memory_order order) {
                __atomic_store_n(value, static_cast<storage>(desired),
                                 atomic_order(order));
            }

            static T exchange(storage* value, T desired, memory_order order) {
                return static_cast<T>(__atomic_exchange_n(
                    value, static_cast<storage>(desired), atomic_order(order)));
            }

            static bool compare_exchange(storage* value, T& expected, T desired,
                                         bool weak, memory_order success,
                                         memory_order failure) {
                storage observed = static_cast<storage>(expected);

                const bool exchanged = __atomic_compare_exchange_n(
                    value, &observed, static_cast<storage>(desired), weak,
                    atomic_order(success), atomic_order(failure));

                expected = static_cast<T>(observed);

                return exchanged;
            }

            static T fetch_add(storage* value, T delta, memory_order order) {
                static_assert(atomic_traits<T>::arithmetic,
                              "arithmetic atomics do not cover pointers");

                return static_cast<T>(__atomic_fetch_add(
                    value, static_cast<storage>(delta), atomic_order(order)));
            }

            static T fetch_sub(storage* value, T delta, memory_order order) {
                static_assert(atomic_traits<T>::arithmetic,
                              "arithmetic atomics do not cover pointers");

                return static_cast<T>(__atomic_fetch_sub(
                    value, static_cast<storage>(delta), atomic_order(order)));
            }

            static T fetch_or(storage* value, T mask, memory_order order) {
                static_assert(atomic_traits<T>::arithmetic,
                              "arithmetic atomics do not cover pointers");

                return static_cast<T>(__atomic_fetch_or(value, static_cast<storage>(mask),
                                                        atomic_order(order)));
            }

            static T fetch_and(storage* value, T mask, memory_order order) {
                static_assert(atomic_traits<T>::arithmetic,
                              "arithmetic atomics do not cover pointers");

                return static_cast<T>(__atomic_fetch_and(
                    value, static_cast<storage>(mask), atomic_order(order)));
            }

            static T fetch_xor(storage* value, T mask, memory_order order) {
                static_assert(atomic_traits<T>::arithmetic,
                              "arithmetic atomics do not cover pointers");

                return static_cast<T>(__atomic_fetch_xor(
                    value, static_cast<storage>(mask), atomic_order(order)));
            }

            static T add_fetch(storage* value, T delta, memory_order order) {
                static_assert(atomic_traits<T>::arithmetic,
                              "arithmetic atomics do not cover pointers");

                return static_cast<T>(__atomic_add_fetch(
                    value, static_cast<storage>(delta), atomic_order(order)));
            }

            static T sub_fetch(storage* value, T delta, memory_order order) {
                static_assert(atomic_traits<T>::arithmetic,
                              "arithmetic atomics do not cover pointers");

                return static_cast<T>(__atomic_sub_fetch(
                    value, static_cast<storage>(delta), atomic_order(order)));
            }

            static T or_fetch(storage* value, T mask, memory_order order) {
                static_assert(atomic_traits<T>::arithmetic,
                              "arithmetic atomics do not cover pointers");

                return static_cast<T>(__atomic_or_fetch(value, static_cast<storage>(mask),
                                                        atomic_order(order)));
            }

            static T and_fetch(storage* value, T mask, memory_order order) {
                static_assert(atomic_traits<T>::arithmetic,
                              "arithmetic atomics do not cover pointers");

                return static_cast<T>(__atomic_and_fetch(
                    value, static_cast<storage>(mask), atomic_order(order)));
            }

            static T xor_fetch(storage* value, T mask, memory_order order) {
                static_assert(atomic_traits<T>::arithmetic,
                              "arithmetic atomics do not cover pointers");

                return static_cast<T>(__atomic_xor_fetch(
                    value, static_cast<storage>(mask), atomic_order(order)));
            }
        };
    } // namespace detail

    /// Compiler fence with the same order vocabulary as the atomic wrappers.
    inline void atomic_thread_fence(memory_order order) noexcept {
        __atomic_thread_fence(detail::atomic_order(order));
    }

    /// Minimal freestanding atomic over the compiler's `__atomic` builtins.
    /// The kernel does not use libstdc++'s `<atomic>` (it drags hosted
    /// assertions into the link), so this is the shared primitive for integer,
    /// enum, and pointer values that the target can access atomically.
    template <typename T>
    class atomic {
        using storage = detail::atomic_storage<T>;

        static_assert(detail::atomic_traits<T>::supported,
                      "astia::atomic covers integral, enum, and pointer types");
        static_assert(sizeof(storage) <= sizeof(u64),
                      "astia::atomic covers at most 64-bit values");

        storage value_{};

        public:
        constexpr atomic() = default;

        constexpr explicit atomic(T value) : value_(static_cast<storage>(value)) {}

        atomic(const atomic&)            = delete;
        atomic& operator=(const atomic&) = delete;

        T load(memory_order order = memory_order::acquire) const {
            return detail::atomic_access<T>::load(&value_, order);
        }

        void store(T value, memory_order order = memory_order::release) {
            detail::atomic_access<T>::store(&value_, value, order);
        }

        T exchange(T value, memory_order order = memory_order::acq_rel) {
            return detail::atomic_access<T>::exchange(&value_, value, order);
        }

        T fetch_add(T delta, memory_order order = memory_order::acq_rel) {
            return detail::atomic_access<T>::fetch_add(&value_, delta, order);
        }

        T fetch_sub(T delta, memory_order order = memory_order::acq_rel) {
            return detail::atomic_access<T>::fetch_sub(&value_, delta, order);
        }

        T fetch_or(T mask, memory_order order = memory_order::acq_rel) {
            return detail::atomic_access<T>::fetch_or(&value_, mask, order);
        }

        T fetch_and(T mask, memory_order order = memory_order::acq_rel) {
            return detail::atomic_access<T>::fetch_and(&value_, mask, order);
        }

        T fetch_xor(T mask, memory_order order = memory_order::acq_rel) {
            return detail::atomic_access<T>::fetch_xor(&value_, mask, order);
        }

        T add_fetch(T delta, memory_order order = memory_order::acq_rel) {
            return detail::atomic_access<T>::add_fetch(&value_, delta, order);
        }

        T sub_fetch(T delta, memory_order order = memory_order::acq_rel) {
            return detail::atomic_access<T>::sub_fetch(&value_, delta, order);
        }

        T or_fetch(T mask, memory_order order = memory_order::acq_rel) {
            return detail::atomic_access<T>::or_fetch(&value_, mask, order);
        }

        T and_fetch(T mask, memory_order order = memory_order::acq_rel) {
            return detail::atomic_access<T>::and_fetch(&value_, mask, order);
        }

        T xor_fetch(T mask, memory_order order = memory_order::acq_rel) {
            return detail::atomic_access<T>::xor_fetch(&value_, mask, order);
        }

        bool compare_exchange_strong(T& expected, T desired,
                                     memory_order success = memory_order::acq_rel,
                                     memory_order failure = memory_order::acquire) {
            return detail::atomic_access<T>::compare_exchange(&value_, expected, desired,
                                                              false, success, failure);
        }

        bool compare_exchange_weak(T& expected, T desired,
                                   memory_order success = memory_order::acq_rel,
                                   memory_order failure = memory_order::acquire) {
            return detail::atomic_access<T>::compare_exchange(&value_, expected, desired,
                                                              true, success, failure);
        }
    };

    /// The same operations on an object owned elsewhere (ABI layouts and
    /// otherwise plain fields), so the layout stays untouched.
    template <typename T>
    class atomic_ref {
        using storage = detail::atomic_storage<T>;

        static_assert(detail::atomic_traits<T>::supported,
                      "astia::atomic_ref covers integral, enum, and pointer types");
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
            return detail::atomic_access<T>::load(raw(), order);
        }

        void store(T value, memory_order order = memory_order::release) {
            detail::atomic_access<T>::store(raw(), value, order);
        }

        T exchange(T value, memory_order order = memory_order::acq_rel) {
            return detail::atomic_access<T>::exchange(raw(), value, order);
        }

        T fetch_add(T delta, memory_order order = memory_order::acq_rel) {
            return detail::atomic_access<T>::fetch_add(raw(), delta, order);
        }

        T fetch_sub(T delta, memory_order order = memory_order::acq_rel) {
            return detail::atomic_access<T>::fetch_sub(raw(), delta, order);
        }

        T fetch_or(T mask, memory_order order = memory_order::acq_rel) {
            return detail::atomic_access<T>::fetch_or(raw(), mask, order);
        }

        T fetch_and(T mask, memory_order order = memory_order::acq_rel) {
            return detail::atomic_access<T>::fetch_and(raw(), mask, order);
        }

        T fetch_xor(T mask, memory_order order = memory_order::acq_rel) {
            return detail::atomic_access<T>::fetch_xor(raw(), mask, order);
        }

        T add_fetch(T delta, memory_order order = memory_order::acq_rel) {
            return detail::atomic_access<T>::add_fetch(raw(), delta, order);
        }

        T sub_fetch(T delta, memory_order order = memory_order::acq_rel) {
            return detail::atomic_access<T>::sub_fetch(raw(), delta, order);
        }

        T or_fetch(T mask, memory_order order = memory_order::acq_rel) {
            return detail::atomic_access<T>::or_fetch(raw(), mask, order);
        }

        T and_fetch(T mask, memory_order order = memory_order::acq_rel) {
            return detail::atomic_access<T>::and_fetch(raw(), mask, order);
        }

        T xor_fetch(T mask, memory_order order = memory_order::acq_rel) {
            return detail::atomic_access<T>::xor_fetch(raw(), mask, order);
        }

        bool compare_exchange_strong(T& expected, T desired,
                                     memory_order success = memory_order::acq_rel,
                                     memory_order failure = memory_order::acquire) {
            return detail::atomic_access<T>::compare_exchange(raw(), expected, desired,
                                                              false, success, failure);
        }

        bool compare_exchange_weak(T& expected, T desired,
                                   memory_order success = memory_order::acq_rel,
                                   memory_order failure = memory_order::acquire) {
            return detail::atomic_access<T>::compare_exchange(raw(), expected, desired,
                                                              true, success, failure);
        }

        private:
        T* value_;
    };
} // namespace astia
