#pragma once

#include <astia/common.hpp>

namespace astia {

    /// Non-owning contiguous view. The owner must outlive the span.
    template <typename T>
    class span {
        public:
        constexpr span() = default;
        constexpr span(T* data, usize size) : data_(data), size_(size) {}
        static span bytes(u64 address, usize length) {
            return span(reinterpret_cast<T*>(address), length);
        }
        static span string(u64 address) {
            return span(reinterpret_cast<T*>(address), 0);
        }
        T* data() const {
            return data_;
        }
        usize size() const {
            return size_;
        }
        u64 address() const {
            return reinterpret_cast<u64>(data_);
        }
        usize length() const {
            return size_;
        }
        bool null() const {
            return data_ == nullptr;
        }
        bool empty() const {
            return size_ == 0;
        }
        T& operator[](usize index) const {
            return data_[index];
        }

        private:
        T*    data_ = nullptr;
        usize size_ = 0;
    };

} // namespace astia
