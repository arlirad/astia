#pragma once

#include <astia/atomic.hpp>
#include <astia/common.hpp>

#include <new>

// Small shared pointer for kernel objects.  Copies share a reference
// counter; the last owner destroys the object.  `borrowed` is for
// statically owned objects such as the early UART: it shares the object
// without taking responsibility for deleting it.

namespace astia::mm {
    struct shcontrol {
        atomic<u32> refs;
        atomic<u32> weaks;
        void*       value;
        void*       owned;
        void (*destroy)(void*);
    };

    template <typename T>
    class shweak;

    template <typename T>
    class shptr {
        template <typename>
        friend class shptr;

        template <typename>
        friend class shweak;

        public:
        constexpr shptr() = default;

        template <typename U>
            requires requires(U* value) { static_cast<T*>(value); }

        explicit shptr(U* value) {
            acquire(value, &destroy_owned<U>);
        }

        static shptr borrowed(T* value) {
            shptr result;

            result.acquire(value, nullptr);

            return result;
        }

        template <typename U>
        static shptr adopt(U* value, void (*destroy)(void*)) {
            shptr result;

            result.acquire(value, destroy);

            return result;
        }

        shptr(const shptr& other) : _control(other._control) {
            retain();
        }

        template <typename U>
            requires requires(U* value) { static_cast<T*>(value); }

        shptr(const shptr<U>& other) : _control(other._control) {
            retain();
        }

        shptr& operator=(const shptr& other) {
            if (this != &other) {
                release();

                _control = other._control;

                retain();
            }

            return *this;
        }

        template <typename U>
            requires requires(U* value) { static_cast<T*>(value); }
        shptr& operator=(const shptr<U>& other) {
            release();

            _control = other._control;

            retain();

            return *this;
        }

        shptr(shptr&& other) noexcept : _control(other._control) {
            other._control = nullptr;
        }

        template <typename U>
            requires requires(U* value) { static_cast<T*>(value); }

        shptr(shptr<U>&& other) noexcept : _control(other._control) {
            other._control = nullptr;
        }

        shptr& operator=(shptr&& other) noexcept {
            if (this != &other) {
                release();

                _control       = other._control;
                other._control = nullptr;
            }

            return *this;
        }

        ~shptr() {
            release();
        }

        T* get() const {
            return _control == nullptr ? nullptr : static_cast<T*>(_control->value);
        }

        T* operator->() const {
            return get();
        }

        T& operator*() const {
            return *get();
        }

        explicit operator bool() const {
            return get() != nullptr;
        }

        shweak<T> weak() const;

        private:
        static shptr from_control(shcontrol* control) {
            shptr result;

            result._control = control;

            return result;
        }

        shcontrol* _control = nullptr;

        template <typename U>
        static void destroy_owned(void* value) {
            delete static_cast<U*>(value);
        }

        template <typename U>
        void acquire(U* value, void (*destroy)(void*)) {
            if (value == nullptr)
                return;

            shcontrol* control = new (std::nothrow) shcontrol;
            if (control == nullptr) {
                if (destroy != nullptr)
                    destroy(value);

                return;
            }

            control->refs.store(1, memory_order::relaxed);
            control->weaks.store(1, memory_order::relaxed);

            control->value   = static_cast<T*>(value);
            control->owned   = value;
            control->destroy = destroy;
            _control         = control;
        }

        void retain() {
            if (_control != nullptr)
                (void) _control->refs.fetch_add(1, memory_order::relaxed);
        }

        void release() {
            if (_control == nullptr)
                return;

            shcontrol* control = _control;

            _control = nullptr;

            if (control->refs.fetch_sub(1, memory_order::acq_rel) == 1) {
                if (control->destroy != nullptr)
                    control->destroy(control->owned);

                if (control->weaks.fetch_sub(1, memory_order::acq_rel) == 1)
                    delete control;
            }
        }
    };
} // namespace astia::mm

#include "shweak.hpp"
