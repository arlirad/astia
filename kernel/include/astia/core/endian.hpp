#pragma once

#include <astia/common.hpp>

namespace astia::core {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    inline constexpr bool host_little_endian = true;
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    inline constexpr bool host_little_endian = false;
#else
#error "unsupported host byte order"
#endif

    enum class byte_order : u8 { little, big };

    template <typename T>
    constexpr T byteswap(T value) {
        static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 ||
                      sizeof(T) == 8);
        T result = 0;

        for (usz index = 0; index < sizeof(T); ++index) {
            result = static_cast<T>(result << 8);
            result = static_cast<T>(result | (value & static_cast<T>(0xff)));
            value  = static_cast<T>(value >> 8);
        }

        return result;
    }

    namespace detail {
        /// Storage type used on the wire.  Enums are stored as their
        /// underlying integer type; every other type as itself.
        template <typename T, bool IsEnum = __is_enum(T)>
        struct wire_type {
            using type = T;
        };

        template <typename T>
        struct wire_type<T, true> {
            using type = __underlying_type(T);
        };

        template <typename T>
        T load(const u8* source, byte_order order) {
            T     native{};
            auto* destination = reinterpret_cast<u8*>(&native);

            for (usz index = 0; index < sizeof(T); ++index) {
                destination[index] = source[index];
            }

            const bool little = order == byte_order::little;
            if (little != host_little_endian)
                native = byteswap(native);

            return native;
        }

        template <typename T>
        void store(u8* destination, T value, byte_order order) {
            const bool little = order == byte_order::little;
            if (little != host_little_endian)
                value = byteswap(value);

            const auto* source = reinterpret_cast<const u8*>(&value);

            for (usz index = 0; index < sizeof(T); ++index) {
                destination[index] = source[index];
            }
        }
    } // namespace detail

    template <typename T>
    class le {
        using storage_type = typename detail::wire_type<T>::type;

        static_assert(sizeof(storage_type) == 1 || sizeof(storage_type) == 2 ||
                      sizeof(storage_type) == 4 || sizeof(storage_type) == 8);

        public:
        le() = default;

        explicit le(T value) {
            set(value);
        }

        T get() const {
            return static_cast<T>(detail::load<storage_type>(_bytes, byte_order::little));
        }

        T get() const volatile {
            u8 copy[sizeof(T)]{};

            for (usz index = 0; index < sizeof(T); ++index) {
                copy[index] = _bytes[index];
            }

            return static_cast<T>(detail::load<storage_type>(copy, byte_order::little));
        }

        operator T() const {
            return get();
        }

        operator T() const volatile {
            return get();
        }

        le& operator=(T value) {
            set(value);

            return *this;
        }

        void set(T value) {
            detail::store(_bytes, static_cast<storage_type>(value), byte_order::little);
        }

        static T load(const void* source) {
            return static_cast<T>(detail::load<storage_type>(
                static_cast<const u8*>(source), byte_order::little));
        }

        void store(void* destination) const {
            detail::store(static_cast<u8*>(destination), static_cast<storage_type>(get()),
                          byte_order::little);
        }

        private:
        u8 _bytes[sizeof(T)]{};
    };

    template <typename T>
    class be {
        using storage_type = typename detail::wire_type<T>::type;

        static_assert(sizeof(storage_type) == 1 || sizeof(storage_type) == 2 ||
                      sizeof(storage_type) == 4 || sizeof(storage_type) == 8);

        public:
        be() = default;

        explicit be(T value) {
            set(value);
        }

        T get() const {
            return static_cast<T>(detail::load<storage_type>(_bytes, byte_order::big));
        }

        T get() const volatile {
            u8 copy[sizeof(T)]{};

            for (usz index = 0; index < sizeof(T); ++index) {
                copy[index] = _bytes[index];
            }

            return static_cast<T>(detail::load<storage_type>(copy, byte_order::big));
        }

        operator T() const {
            return get();
        }

        operator T() const volatile {
            return get();
        }

        be& operator=(T value) {
            set(value);

            return *this;
        }

        void set(T value) {
            detail::store(_bytes, static_cast<storage_type>(value), byte_order::big);
        }

        static T load(const void* source) {
            return static_cast<T>(detail::load<storage_type>(
                static_cast<const u8*>(source), byte_order::big));
        }

        void store(void* destination) const {
            detail::store(static_cast<u8*>(destination), static_cast<storage_type>(get()),
                          byte_order::big);
        }

        private:
        u8 _bytes[sizeof(T)]{};
    };
} // namespace astia::core
