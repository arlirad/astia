#pragma once

#include <astia/common.hpp>

namespace astia {
    /// Non-owning contiguous view. The owner must outlive the span.
    template <typename T>
    class span {
        public:
        constexpr span() = default;

        constexpr span(T* data, usize size) : _data(data), _size(size) {}

        static span bytes(u64 address, usize length) {
            return span(reinterpret_cast<T*>(address), length);
        }

        static span string(u64 address) {
            return span(reinterpret_cast<T*>(address), 0);
        }

        T* data() const {
            return _data;
        }

        usize size() const {
            return _size;
        }

        u64 address() const {
            return reinterpret_cast<u64>(_data);
        }

        usize length() const {
            return _size;
        }

        bool null() const {
            return _data == nullptr;
        }

        bool empty() const {
            return _size == 0;
        }

        T& operator[](usize index) const {
            return _data[index];
        }

        private:
        T*    _data = nullptr;
        usize _size = 0;
    };
} // namespace astia
