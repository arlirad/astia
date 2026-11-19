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
        static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8);
        T result = 0;
        for (usize index = 0; index < sizeof(T); ++index) {
            result = static_cast<T>(result << 8);
            result = static_cast<T>(result | (value & static_cast<T>(0xff)));
            value  = static_cast<T>(value >> 8);
        }
        return result;
    }

    namespace detail {

        template <typename T>
        T load(const u8* source, byte_order order) {
            T native{};
            auto* destination = reinterpret_cast<u8*>(&native);
            for (usize index = 0; index < sizeof(T); ++index)
                destination[index] = source[index];
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
            for (usize index = 0; index < sizeof(T); ++index)
                destination[index] = source[index];
        }

    } // namespace detail

    template <typename T>
    class le {
        static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8);

        public:
        le() = default;
        explicit le(T value) {
            set(value);
        }

        T get() const {
            return detail::load<T>(_bytes, byte_order::little);
        }
        T get() const volatile {
            u8 copy[sizeof(T)]{};
            for (usize index = 0; index < sizeof(T); ++index)
                copy[index] = _bytes[index];
            return detail::load<T>(copy, byte_order::little);
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
            detail::store(_bytes, value, byte_order::little);
        }
        static T load(const void* source) {
            return detail::load<T>(static_cast<const u8*>(source), byte_order::little);
        }
        void store(void* destination) const {
            detail::store(static_cast<u8*>(destination), get(), byte_order::little);
        }

        private:
        u8 _bytes[sizeof(T)]{};
    };

    template <typename T>
    class be {
        static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8);

        public:
        be() = default;
        explicit be(T value) {
            set(value);
        }

        T get() const {
            return detail::load<T>(_bytes, byte_order::big);
        }
        T get() const volatile {
            u8 copy[sizeof(T)]{};
            for (usize index = 0; index < sizeof(T); ++index)
                copy[index] = _bytes[index];
            return detail::load<T>(copy, byte_order::big);
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
            detail::store(_bytes, value, byte_order::big);
        }
        static T load(const void* source) {
            return detail::load<T>(static_cast<const u8*>(source), byte_order::big);
        }
        void store(void* destination) const {
            detail::store(static_cast<u8*>(destination), get(), byte_order::big);
        }

        private:
        u8 _bytes[sizeof(T)]{};
    };

} // namespace astia::core
