#pragma once

#include <astia/atomic.hpp>
#include <astia/common.hpp>
#include <astia/core/cancellation.hpp>
#include <astia/core/completion.hpp>
#include <astia/mm/uptr.hpp>

#if defined(__SANITIZE_THREAD__)
extern "C" void __tsan_acquire(void const*);
extern "C" void __tsan_release(void const*);
#define ASTIA_IO_TSAN_ACQUIRE(value) __tsan_acquire(value)
#define ASTIA_IO_TSAN_RELEASE(value) __tsan_release(value)
#else
#define ASTIA_IO_TSAN_ACQUIRE(value) ((void) 0)
#define ASTIA_IO_TSAN_RELEASE(value) ((void) 0)
#endif

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

namespace astia::fs {}

namespace astia::io {
    class generation final {
        public:
        u64 issue() {
            const u64 result = _next;

            _current = result;
            _next    = _next == ~u64{0} ? 1 : _next + 1;

            return result;
        }

        bool current(u64 value) const {
            return value != 0 && value == _current;
        }

        u64 value() const {
            return _current;
        }

        private:
        u64 _next    = 1;
        u64 _current = 0;
    };

    class operation;
    class request;

    enum class direction : u8 {
        input,  // fill request.buffer() from the object
        output, // drain request.buffer() into the object
    };

    class request {
        public:
        request(const request&)            = delete;
        request& operator=(const request&) = delete;
        request(request&&)                 = delete;
        request& operator=(request&&)      = delete;

        request() = default;

        explicit request(core::clsource& source) : _external_token(source.token()) {}

        request(core::clsource& source, void* buffer, u32 length, u64 offset,
                u64 user_data, direction direction)
            : _buffer(buffer), _length(length), _offset(offset), _user_data(user_data),
              _direction(direction), _external_token(source.token()) {}

        request(void* buffer, u32 length, u64 offset, u64 user_data, direction direction)
            : _buffer(buffer), _length(length), _offset(offset), _user_data(user_data),
              _direction(direction) {}

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

        u64 generation() const {
            return _generation;
        }

        void generation(u64 value) {
            _generation = value;
        }

        astia::io::direction direction() const {
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

        core::completion<i64>* completion() const {
            return _completion.get();
        }

        void completion(mm::uptr<core::completion<i64>> value) {
            _completion = static_cast<mm::uptr<core::completion<i64>>&&>(value);
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

        bool stable() const {
            return _operation != nullptr;
        }

        bool retain_for_device();
        void release_for_device();

        void stretch(void* buffer, u32 length, u64 offset) {
            _buffer          = buffer;
            _length          = length;
            _offset          = offset;
            _device_state    = nullptr;
            _complete        = false;
            _timeout_expired = false;
            _poll_wakeup     = false;
        }

        bool timeout_set() const {
            return _timeout_set;
        }

        u64 timeout_ns() const {
            return _timeout_ns;
        }

        void timeout_ns(u64 value) {
            _timeout_set = true;
            _timeout_ns  = value;
        }

        bool timeout_expired() const {
            return _timeout_expired;
        }

        void timeout_expired(bool value) {
            _timeout_expired = value;
        }

        bool poll_wakeup() const {
            return _poll_wakeup;
        }

        void poll_wakeup(bool value) {
            _poll_wakeup = value;
        }

        bool poll_interval_set() const {
            return _poll_interval_set;
        }

        u64 poll_interval_ns() const {
            return _poll_interval_ns;
        }

        void poll_interval_ns(u64 value) {
            _poll_interval_set = value != 0;
            _poll_interval_ns  = value;
        }

        void wake_handler_set(void (*handler)(void*), void* context) {
            _wake_handler = handler;
            _wake_context = context;
        }

        void wake() const {
            if (_wake_handler != nullptr)
                _wake_handler(_wake_context);
        }

        bool wake(u64 generation) const {
            if (_generation != generation)
                return false;

            wake();

            return true;
        }

        bool park_device();

        bool canceled() const {
            return _cancel_requested || _external_token.canceled();
        }

        bool begin_device();
        void end_device();
        void acknowledge();

        core::cltoken cancellation_token() const {
            return _external_token;
        }

        void external_token(core::cltoken token) {
            _external_token = token;
        }

        bool cancellation_requested() const {
            return canceled();
        }

        core::cancel_reason cancellation_reason() const {
            const core::cancel_reason external = _external_token.reason();

            return external != core::cancel_reason::none ? external : _cancel_reason;
        }

        bool cancel_device(bool                       = false,
                           core::cancel_reason reason = core::cancel_reason::request) {
            if (_cancel_requested)
                return false;

            _cancel_requested = true;
            _cancel_reason    = reason;

            wake();

            return true;
        }

        private:
        void*                _buffer                  = nullptr;
        u32                  _length                  = 0;
        u64                  _offset                  = 0;
        u64                  _user_data               = 0;
        u64                  _generation              = 0;
        void*                _device_state            = nullptr;
        astia::io::direction _direction               = astia::io::direction::input;
        bool                 _updates_position        = true;
        bool                 _append_reserved         = false;
        u32                  _append_length           = 0;
        bool                 _append_length_set       = false;
        u8                   _stage                   = 0;
        bool                 _complete                = false;
        bool                 _timeout_set             = false;
        bool                 _timeout_expired         = false;
        u64                  _timeout_ns              = 0;
        bool                 _poll_interval_set       = false;
        u64                  _poll_interval_ns        = 0;
        bool                 _poll_wakeup             = false;
        void (*_wake_handler)(void*)                  = nullptr;
        void*                           _wake_context = nullptr;
        operation*                      _operation    = nullptr;
        mm::uptr<core::completion<i64>> _completion;
        core::cltoken                   _external_token{};
        bool                            _cancel_requested = false;
        core::cancel_reason             _cancel_reason    = core::cancel_reason::none;

        request* _queue_next = nullptr;

        friend class operation;
        friend class request_queue;
    };

    class operation {
        enum class state : u8 {
            owner,
            parked,
            active,
            active_completed,
            canceled,
            completed,
        };

        astia::io::request _request;
        mutable u8         _state          = static_cast<u8>(state::owner);
        u32                _refs           = 1;
        bool               _heap_owned     = false;
        bool               _owner_released = false;

        void release_ref() {
            if (astia::atomic_ref<u32>(_refs).sub_fetch(
                    1u, astia::memory_order::acq_rel) == 0 &&
                _heap_owned)
                delete this;
        }

        friend class request;

        public:
        void heap_owned(bool value) {
            _heap_owned = value;
        }

        bool retain_for_device() {
            if (_request._operation != this)
                return false;

            astia::atomic_ref<u32>(_refs).add_fetch(1u, astia::memory_order::acq_rel);

            return true;
        }

        void release_for_device() {
            release_ref();
        }

        void release_owner() {
            bool expected = false;

            if (!astia::atomic_ref<bool>(_owner_released)
                     .compare_exchange_strong(expected, true,
                                              astia::memory_order::acq_rel,
                                              astia::memory_order::acquire))
                return;

            const auto observed = static_cast<state>(
                astia::atomic_ref<u8>(_state).load(astia::memory_order::acquire));

            if (observed == state::parked || observed == state::active ||
                observed == state::active_completed)
                request().cancel_device(true, core::cancel_reason::owner);

            release_ref();
        }

        operation() {
            _request._operation = this;
        }

        operation(void* buffer, u32 length, u64 offset, direction direction,
                  mm::uptr<core::completion<i64>> completion = {},
                  core::cltoken                   token      = {})
            : _request(buffer, length, offset, 0, direction) {
            _request._operation = this;

            _request.external_token(token);
            _request.completion(
                static_cast<mm::uptr<core::completion<i64>>&&>(completion));
        }

        operation(void* buffer, u32 length, u64 offset, u64 user_data,
                  direction direction, mm::uptr<core::completion<i64>> completion = {},
                  core::cltoken token = {})
            : _request(buffer, length, offset, user_data, direction) {
            _request._operation = this;

            _request.external_token(token);
            _request.completion(
                static_cast<mm::uptr<core::completion<i64>>&&>(completion));
        }

        operation(const operation&)            = delete;
        operation& operator=(const operation&) = delete;

        ~operation() {
            release_owner();
        }

        astia::io::request& request() {
            return _request;
        }

        const astia::io::request& request() const {
            return _request;
        }

        void reset(void* buffer, u32 length, u64 offset, u64 user_data,
                   direction direction) {
            release_owner();
            _request.stretch(buffer, length, offset);

            _request._user_data         = user_data;
            _request._direction         = direction;
            _request._generation        = 0;
            _request._stage             = 0;
            _request._timeout_set       = false;
            _request._timeout_ns        = 0;
            _request._poll_interval_set = false;
            _request._poll_interval_ns  = 0;
            _request._updates_position  = true;
            _request._append_reserved   = false;
            _request._append_length     = 0;
            _request._append_length_set = false;

            mm::uptr<core::completion<i64>> completion;

            _request.completion(
                static_cast<mm::uptr<core::completion<i64>>&&>(completion));

            _request._operation = this;

            astia::atomic_ref<u32>(_refs).store(1u, astia::memory_order::release);
            astia::atomic_ref<bool>(_owner_released)
                .store(false, astia::memory_order::release);
            astia::atomic_ref<u8>(_state).store(static_cast<u8>(state::owner),
                                                astia::memory_order::release);
        }

        bool park_device() {
            u8 expected = static_cast<u8>(state::owner);

            if (!astia::atomic_ref<u8>(_state).compare_exchange_strong(
                    expected, static_cast<u8>(state::parked),
                    astia::memory_order::acq_rel, astia::memory_order::acquire))
                return false;

            astia::atomic_ref<u32>(_refs).add_fetch(1u, astia::memory_order::acq_rel);

            return true;
        }

        bool canceled() const {
            return static_cast<state>(astia::atomic_ref<u8>(_state).load(
                       astia::memory_order::acquire)) == state::canceled;
        }

        bool begin_device() {
            u8 expected = static_cast<u8>(state::parked);

            return astia::atomic_ref<u8>(_state).compare_exchange_strong(
                expected, static_cast<u8>(state::active), astia::memory_order::acq_rel,
                astia::memory_order::acquire);
        }

        void end_device() {
            u8 expected = static_cast<u8>(state::active);

            if (astia::atomic_ref<u8>(_state).compare_exchange_strong(
                    expected, static_cast<u8>(state::parked),
                    astia::memory_order::release, astia::memory_order::acquire))
                return;

            if (expected == static_cast<u8>(state::active_completed)) {
                u8 completed = static_cast<u8>(state::active_completed);

                if (astia::atomic_ref<u8>(_state).compare_exchange_strong(
                        completed, static_cast<u8>(state::completed),
                        astia::memory_order::release, astia::memory_order::acquire))
                    release_ref();
            }
        }

        void acknowledge() {
            u8 observed =
                astia::atomic_ref<u8>(_state).load(astia::memory_order::acquire);

            while (true) {
                const auto current = static_cast<state>(observed);

                if (current == state::completed || current == state::owner ||
                    current == state::active_completed)
                    return;

                if (current == state::active) {
                    u8 expected = static_cast<u8>(state::active);

                    if (astia::atomic_ref<u8>(_state).compare_exchange_strong(
                            expected, static_cast<u8>(state::active_completed),
                            astia::memory_order::acq_rel, astia::memory_order::acquire))
                        return;

                    observed = expected;

                    continue;
                }

                if (astia::atomic_ref<u8>(_state).compare_exchange_strong(
                        observed, static_cast<u8>(state::completed),
                        astia::memory_order::acq_rel, astia::memory_order::acquire)) {
                    release_ref();

                    return;
                }
            }
        }
    };

    inline bool request::park_device() {
        return _operation != nullptr && _operation->park_device();
    }

    inline bool request::retain_for_device() {
        return _operation != nullptr && _operation->retain_for_device();
    }

    inline void request::release_for_device() {
        if (_operation != nullptr)
            _operation->release_for_device();
    }

    inline bool request::begin_device() {
        return _operation != nullptr && _operation->begin_device();
    }

    inline void request::end_device() {
        if (_operation != nullptr)
            _operation->end_device();
    }

    inline void request::acknowledge() {
        if (_operation != nullptr)
            _operation->acknowledge();
    }
} // namespace astia::io

#undef ASTIA_IO_TSAN_ACQUIRE
#undef ASTIA_IO_TSAN_RELEASE
