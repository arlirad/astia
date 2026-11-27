#pragma once

#include <astia/api/errno.h>
#include <astia/optional.hpp>

#include "../../proc/proc.hpp"
#include "aio.hpp"

namespace astia::io::aio::memory {
    inline bool user_span_ok_for(proc::process* owner, span<u8> range);

    template <typename T>
    class validated_user_ptr {
        template <typename>
        friend class user_ptr;

        public:
        constexpr validated_user_ptr() = default;

        T* get() const {
            return reinterpret_cast<T*>(_address);
        }

        span<u8> bytes(usz count = 1) const {
            return span<u8>::bytes(_address, sizeof(T) * count);
        }

        private:
        explicit validated_user_ptr(u64 address) : _address(address) {}

        u64 _address = 0;
    };

    template <typename T>
    class user_ptr {
        public:
        explicit constexpr user_ptr(u64 address) : _address(address) {}

        constexpr u64 address() const {
            return _address;
        }

        optional<validated_user_ptr<T>> validate(usz count = 1) const {
            return validate_for(proc::current_process(), count);
        }

        optional<validated_user_ptr<T>> validate_for(proc::process* owner,
                                                     usz            count = 1) const {
            if (count > (~usz{0} / sizeof(T)))
                return error_t::erange;

            const span<u8> range = span<u8>::bytes(_address, sizeof(T) * count);

            return user_span_ok_for(owner, range)
                       ? optional<validated_user_ptr<T>>(validated_user_ptr<T>(_address))
                       : optional<validated_user_ptr<T>>(error_t::efault);
        }

        private:
        u64 _address = 0;
    };

    inline bool user_span_ok_for(proc::process* owner, span<u8> range) {
        const auto& adapter = user_memory();

        return adapter.user_span_ok != nullptr && adapter.user_span_ok(owner, range);
    }

    inline bool user_span_ok_for(span<u8> range) {
        return user_span_ok_for(proc::current_process(), range);
    }

    inline bool user_span_ok(span<u8> range) {
        return user_span_ok_for(proc::current_process(), range);
    }

    inline bool user_span_writable_ok_for(proc::process* owner, span<u8> range) {
        const auto& adapter = user_memory();

        return adapter.user_span_writable_ok != nullptr &&
               adapter.user_span_writable_ok(owner, range);
    }

    inline bool user_span_writable_ok(span<u8> range) {
        return user_span_writable_ok_for(proc::current_process(), range);
    }

    inline bool kernel_span_ok(span<u8> range) {
        const auto& adapter = user_memory();

        return adapter.kernel_span_ok != nullptr && adapter.kernel_span_ok(range);
    }

    inline long pin_user_span_for(proc::process* owner, span<u8> range,
                                  ::astia::io::direction direction, mm::iovec& out,
                                  bool allow_kernel_buffer = false) {
        const auto& adapter = user_memory();

        return adapter.pin_user_span == nullptr
                   ? -ENOMEM
                   : adapter.pin_user_span(owner, range, direction, out,
                                           allow_kernel_buffer);
    }

    inline bool copy_in(proc::process* owner, void* destination, span<u8> source) {
        const auto& adapter = user_memory();

        return adapter.copy_in != nullptr && adapter.copy_in(owner, destination, source);
    }

    inline bool copy_in(void* destination, span<u8> source) {
        return copy_in(proc::current_process(), destination, source);
    }

    inline bool copy_in_string(proc::process* owner, span<char> destination,
                               span<u8> source) {
        const auto& adapter = user_memory();

        return adapter.copy_in_string != nullptr &&
               adapter.copy_in_string(owner, destination, source);
    }

    inline bool copy_in_string(span<char> destination, span<u8> source) {
        return copy_in_string(proc::current_process(), destination, source);
    }

    inline bool copy_in_string(span<char> destination, user_ptr<char> source) {
        return copy_in_string(destination, span<u8>::string(source.address()));
    }

    inline bool copy_out_for(proc::process* owner, span<u8> destination,
                             const void* source) {
        const auto& adapter = user_memory();

        return adapter.copy_out != nullptr &&
               adapter.copy_out(owner, destination, source);
    }

    inline bool copy_out(span<u8> destination, const void* source) {
        return copy_out_for(proc::current_process(), destination, source);
    }

    inline bool realtime_now(u64& sec, u64& nsec) {
        const auto& adapter = user_memory();

        return adapter.realtime_now != nullptr && adapter.realtime_now(sec, nsec);
    }
} // namespace astia::io::aio::memory
