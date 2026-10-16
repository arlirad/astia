#pragma once

#include <astia/common.hpp>

// Small shared pointer for kernel objects.  Copies share a reference
// counter; the last owner destroys the object.  `borrowed` is for
// statically owned objects such as the early UART: it shares the object
// without taking responsibility for deleting it.

namespace astia::mm {

    struct shcontrol {
        u32   refs;
        void* value;
        void (*destroy)(void*);
    };

    template <typename T>
    class shptr {
        template <typename>
        friend class shptr;

        public:
        constexpr shptr() = default;

        explicit shptr(T* value) {
            acquire(value, &destroy_owned);
        }

        static shptr borrowed(T* value) {
            shptr result;
            result.acquire(value, nullptr);
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

        private:
        shcontrol* _control = nullptr;

        static void destroy_owned(void* value) {
            delete static_cast<T*>(value);
        }

        void acquire(T* value, void (*destroy)(void*)) {
            if (value != nullptr) {
                _control = new shcontrol{1, value, destroy};
            }
        }

        void retain() {
            if (_control != nullptr) {
                __atomic_add_fetch(&_control->refs, 1, __ATOMIC_RELAXED);
            }
        }

        void release() {
            if (_control == nullptr) {
                return;
            }
            shcontrol* control = _control;
            _control           = nullptr;
            if (__atomic_sub_fetch(&control->refs, 1, __ATOMIC_ACQ_REL) == 0) {
                if (control->destroy != nullptr) {
                    control->destroy(control->value);
                }
                delete control;
            }
        }
    };

} // namespace astia::mm
