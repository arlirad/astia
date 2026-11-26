#pragma once

#include <astia/common.hpp>

namespace astia::mm {
    /// Move-only ownership for a heap object.  Use shptr when ownership is
    /// shared; raw pointers are reserved for borrowed views and callbacks.
    template <typename T>
    class uptr {
        public:
        constexpr uptr() = default;

        explicit uptr(T* value) : _value(value) {}

        uptr(const uptr&)            = delete;
        uptr& operator=(const uptr&) = delete;

        uptr(uptr&& other) noexcept : _value(other.release()) {}

        uptr& operator=(uptr&& other) noexcept {
            if (this != &other)
                reset(other.release());

            return *this;
        }

        ~uptr() {
            reset();
        }

        T* get() const {
            return _value;
        }

        T* operator->() const {
            return _value;
        }
        T& operator*() const {
            return *_value;
        }

        explicit operator bool() const {
            return _value != nullptr;
        }

        bool operator==(decltype(nullptr)) const {
            return _value == nullptr;
        }
        bool operator!=(decltype(nullptr)) const {
            return _value != nullptr;
        }

        void reset(T* value = nullptr) {
            if (_value != value) {
                delete _value;

                _value = value;
            }
        }

        T* release() {
            T* value = _value;

            _value = nullptr;

            return value;
        }

        private:
        T* _value = nullptr;
    };
} // namespace astia::mm
