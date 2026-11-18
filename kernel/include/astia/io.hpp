#pragma once

#include <astia/common.hpp>
#include <astia/core/async_operation.hpp>
#include <astia/mm/uptr.hpp>

// The async operation seam: one in-flight I/O request and the state a
// device needs to resume it.
//
// The ring engine owns submission, parking and completion; the device owns
// the bytes.  `fs::file`/`dev::device::submit()` is the single ring-facing
// entry point, and its return convention is the whole protocol:
//
//   > 0   satisfied: that many bytes moved, the operation is done
//     0   not ready: the request is parked, and the device keeps its
//         reference until the engine resumes it — *unless* the node also
//         marked the request complete, which is how a node with nothing to
//         resume reports the one outcome its return value cannot carry:
//         done, and nothing moved (a read at end of file).  See complete().
//   < 0   -errno: done with an error
//
// A request is a *stable* object for its whole flight.  The engine builds
// it inside the parked-operation slot and hands the same reference back on
// every resume, so a device may stash device_state (a DMA descriptor, a
// sector list, a controller tag) and expect to find it again.  That is the
// property a retry-by-signature contract cannot offer: `read(buf, len, off)`
// identifies an operation only by its arguments, and two parked reads on the
// same descriptor are indistinguishable.
//
// stage is the device's own continuation counter.  It is 0 at first
// submission and whatever the device set before parking, so a multi-step
// operation (program the controller -> move the data -> fix up metadata)
// resumes at the right step instead of re-deriving its position from
// whatever the signature happens to expose.
//
// Nothing here blocks.  A device that parks is resumed from IRQ context; a
// device whose work depends on something else (a filesystem waiting on a
// block layer) blocks a *thread*, never the device.

namespace astia::fs {
    class io_packet;
}

namespace astia {

    class io_flight;

    enum class io_direction : u8 {
        input,  // fill request.buffer() from the object
        output, // drain request.buffer() into the object
    };

    class io_request {
        public:
        io_request() = default;

        io_request(void* buffer, u32 length, u64 offset, u64 user_data,
                   io_direction direction, io_flight* flight = nullptr)
            : _buffer(buffer), _length(length), _offset(offset), _user_data(user_data),
              _direction(direction), _flight(flight) {}

        void* buffer() const {
            return _buffer;
        }
        u32 length() const {
            return _length;
        }
        u64 offset() const {
            return _offset;
        }
        u64 user_data() const {
            return _user_data;
        }
        io_direction direction() const {
            return _direction;
        }

        bool updates_position() const {
            return _updates_position;
        }

        void updates_position(bool value) {
            _updates_position = value;
        }

        bool append_reserved() const {
            return _append_reserved;
        }

        void append_reserved(bool value) {
            _append_reserved = value;
        }

        bool has_append_length() const {
            return _append_length_set;
        }

        u32 append_length() const {
            return _append_length;
        }

        void append_length(u32 value) {
            _append_length     = value;
            _append_length_set = true;
        }

        void offset(u64 value) {
            _offset = value;
        }

        /// Device continuation point: set before parking, read on resume.
        u8 stage() const {
            return _stage;
        }
        void stage(u8 value) {
            _stage = value;
        }

        /// True when the node finished this request inline without moving
        /// anything, and there is no device to resume it: the engine's 0
        /// would otherwise read "wait for me" and the operation would stay
        /// parked forever.  This is the completion a synchronous node
        /// cannot express any other way — a read at end of file.  A node
        /// that parks leaves it clear, and a request rebuilt for the next
        /// entry starts clear again.
        bool complete() const {
            return _complete;
        }

        core::async_operation<i64>* completion() const { return _completion.get(); }
        void completion(mm::uptr<core::async_operation<i64>> value) {
            _completion = static_cast<mm::uptr<core::async_operation<i64>>&&>(value);
        }
        void complete(bool value) {
            _complete = value;
        }

        /// Device-private, and safe to hold across a park — unlike anything
        /// on the submitting stack, which is long gone by resume time.
        void* device_state() const {
            return _device_state;
        }
        void device_state(void* value) {
            _device_state = value;
        }

        bool timeout_set() const {
            return _timeout_set;
        }

        u64 timeout_ns() const {
            return _timeout_ns;
        }

        void timeout_ns(u64 value) {
            _timeout_set = true;
            _timeout_ns = value;
        }

        bool timeout_expired() const {
            return _timeout_expired;
        }

        void timeout_expired(bool value) {
            _timeout_expired = value;
        }

        bool park_device();
        bool canceled() const;
        bool begin_device();
        void end_device();
        void acknowledge();

        using cancel_handler = void (*)(io_request&);
        void cancel_handler_set(cancel_handler handler) {
            _cancel_handler = handler;
        }
        void cancel_device() {
            if (_cancel_handler != nullptr)
                _cancel_handler(*this);
        }

        private:
        void*        _buffer       = nullptr;
        u32          _length       = 0;
        u64          _offset       = 0;
        u64          _user_data    = 0;
        void*        _device_state = nullptr;
        io_direction _direction    = io_direction::input;
        bool         _updates_position = true;
        bool         _append_reserved = false;
        u32          _append_length = 0;
        bool         _append_length_set = false;
        u8           _stage        = 0;
        bool         _complete     = false;
        bool         _timeout_set = false;
        bool         _timeout_expired = false;
        u64          _timeout_ns = 0;
        io_flight*   _flight       = nullptr;
        mm::uptr<core::async_operation<i64>> _completion;
        cancel_handler _cancel_handler = nullptr;
    };

    class io_flight {
        enum class state : u8 {
            owner,
            parked,
            active,
            active_completed,
            canceled,
            completed,
        };

        io_request _request;
        u32         _refs  = 1;
        u8          _state = static_cast<u8>(state::owner);

        void release_ref() {
            if (__atomic_sub_fetch(&_refs, 1, __ATOMIC_ACQ_REL) == 0)
                delete this;
        }

        void cancel_owner() {
            for (;;) {
                const auto observed = static_cast<state>(
                    __atomic_load_n(&_state, __ATOMIC_ACQUIRE));
                if (observed == state::active || observed == state::active_completed) {
                    ASTIA_CPU_RELAX();
                    continue;
                }
                if (observed == state::owner || observed == state::parked) {
                    u8 expected = static_cast<u8>(observed);
                    if (__atomic_compare_exchange_n(
                            &_state, &expected, static_cast<u8>(state::canceled), false,
                            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                        break;
                    continue;
                }
                break;
            }
            release_ref();
        }

        friend class io_request;
        friend class fs::io_packet;

        public:
        io_flight(void* buffer, u32 length, u64 offset, io_direction direction,
                  mm::uptr<core::async_operation<i64>> completion)
            : _request(buffer, length, offset, 0, direction, this) {
            _request.completion(static_cast<mm::uptr<core::async_operation<i64>>&&>(completion));
        }

        io_request& request() { return _request; }

        bool park_device() {
            u8 expected = static_cast<u8>(state::owner);
            if (!__atomic_compare_exchange_n(&_state, &expected,
                                              static_cast<u8>(state::parked), false,
                                              __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                return false;
            __atomic_add_fetch(&_refs, 1, __ATOMIC_RELAXED);
            return true;
        }

        bool canceled() const {
            return static_cast<state>(__atomic_load_n(&_state, __ATOMIC_ACQUIRE)) ==
                   state::canceled;
        }

        bool begin_device() {
            u8 expected = static_cast<u8>(state::parked);
            return __atomic_compare_exchange_n(&_state, &expected,
                                               static_cast<u8>(state::active), false,
                                               __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
        }

        void end_device() {
            u8 expected = static_cast<u8>(state::active);
            if (__atomic_compare_exchange_n(&_state, &expected,
                                             static_cast<u8>(state::parked), false,
                                             __ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
                return;
            if (expected == static_cast<u8>(state::active_completed)) {
                u8 completed = static_cast<u8>(state::active_completed);
                if (__atomic_compare_exchange_n(&_state, &completed,
                                                 static_cast<u8>(state::completed), false,
                                                 __ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
                    release_ref();
            }
        }

        void acknowledge() {
            u8 observed = __atomic_load_n(&_state, __ATOMIC_ACQUIRE);
            for (;;) {
                const auto current = static_cast<state>(observed);
                if (current == state::completed || current == state::owner)
                    return;
                if (current == state::active_completed)
                    return;
                if (current == state::active) {
                    u8 expected = static_cast<u8>(state::active);
                    if (__atomic_compare_exchange_n(
                            &_state, &expected, static_cast<u8>(state::active_completed),
                            false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                        return;
                    observed = expected;
                    continue;
                }
                if (__atomic_compare_exchange_n(&_state, &observed,
                                                 static_cast<u8>(state::completed), false,
                                                 __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                    release_ref();
                    return;
                }
            }
        }
    };

    inline bool io_request::park_device() {
        return _flight != nullptr && _flight->park_device();
    }

    inline bool io_request::canceled() const {
        return _flight != nullptr && _flight->canceled();
    }

    inline bool io_request::begin_device() {
        return _flight != nullptr && _flight->begin_device();
    }

    inline void io_request::end_device() {
        if (_flight != nullptr)
            _flight->end_device();
    }

    inline void io_request::acknowledge() {
        if (_flight != nullptr)
            _flight->acknowledge();
    }

} // namespace astia
