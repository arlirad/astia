#pragma once

#include <astia/common.hpp>

namespace astia::mm {

    /// Move-only ownership for a heap object.  Use shptr when ownership is
    /// shared; raw pointers are reserved for borrowed views and callbacks.
    template <typename T>
    class uptr {
        public:
        constexpr uptr() = default;
        explicit uptr(T* value) : value_(value) {}
        uptr(const uptr&)            = delete;
        uptr& operator=(const uptr&) = delete;

        uptr(uptr&& other) noexcept : value_(other.release()) {}
        uptr& operator=(uptr&& other) noexcept {
            if (this != &other)
                reset(other.release());
            return *this;
        }

        ~uptr() { reset(); }

        T* get() const { return value_; }
        T* operator->() const { return value_; }
        T& operator*() const { return *value_; }
        explicit operator bool() const { return value_ != nullptr; }
        bool operator==(decltype(nullptr)) const { return value_ == nullptr; }
        bool operator!=(decltype(nullptr)) const { return value_ != nullptr; }

        void reset(T* value = nullptr) {
            if (value_ != value) {
                delete value_;
                value_ = value;
            }
        }
        T* release() {
            T* value = value_;
            value_    = nullptr;
            return value;
        }

        private:
        T* value_ = nullptr;
    };

} // namespace astia::mm
