#pragma once

#include <astia/mm/shptr.hpp>

namespace astia::mm {
    template <typename T>
    class shweak {
        template <typename>
        friend class shptr;

        public:
        shweak() = default;

        shweak(const shptr<T>& value) : _control(value._control) {
            retain();
        }

        shweak(const shweak& other) : _control(other._control) {
            retain();
        }

        shweak(shweak&& other) noexcept : _control(other._control) {
            other._control = nullptr;
        }

        shweak& operator=(const shweak& other) {
            if (this != &other) {
                release();

                _control = other._control;

                retain();
            }

            return *this;
        }

        shweak& operator=(shweak&& other) noexcept {
            if (this != &other) {
                release();

                _control       = other._control;
                other._control = nullptr;
            }

            return *this;
        }

        ~shweak() {
            release();
        }

        shptr<T> lock() const {
            if (_control == nullptr)
                return {};

            u32 current = _control->refs.load(memory_order::acquire);

            while (current != 0) {
                if (_control->refs.compare_exchange_weak(current, current + 1,
                                                         memory_order::acq_rel,
                                                         memory_order::acquire))
                    return shptr<T>::from_control(_control);
            }

            return {};
        }

        bool expired() const {
            return _control == nullptr || _control->refs.load(memory_order::acquire) == 0;
        }

        private:
        void retain() {
            if (_control != nullptr)
                (void) _control->weaks.fetch_add(1, memory_order::relaxed);
        }

        void release() {
            if (_control == nullptr)
                return;

            shcontrol* control = _control;

            _control = nullptr;

            if (control->weaks.fetch_sub(1, memory_order::acq_rel) == 1 &&
                control->refs.load(memory_order::acquire) == 0)
                delete control;
        }

        shcontrol* _control = nullptr;
    };

    template <typename T>
    shweak<T> shptr<T>::weak() const {
        return shweak<T>(*this);
    }
} // namespace astia::mm
