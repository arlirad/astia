#pragma once

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

namespace astia::fs {
    class io_packet;
}

namespace astia::io {

    class generation final {
        public:
        u64 issue() {
            const u64 result = _next;
            _current         = result;
            _next            = _next == ~u64{0} ? 1 : _next + 1;
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

    class cancellation_handler {
        public:
        virtual void cancellation_requested(request&, core::cancel_reason, bool) = 0;

        protected:
        ~cancellation_handler() = default;
    };

    class cancellation_observer {
        public:
        virtual void cancellation_acknowledged(request&) = 0;

        protected:
        ~cancellation_observer() = default;
    };

    using legacy_cancellation_handler  = void (*)(request&);
    using legacy_cancellation_observer = void (*)(request&, void*);

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

        explicit request(core::clsource& source)
            : _cancel_source(&source), _cancel_token(source.token()) {}

        request(core::clsource& source, void* buffer, u32 length, u64 offset,
                u64 user_data, direction direction)
            : _buffer(buffer), _length(length), _offset(offset), _user_data(user_data),
              _direction(direction), _cancel_source(&source),
              _cancel_token(source.token()) {}

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
            _buffer       = buffer;
            _length       = length;
            _offset       = offset;
            _device_state = nullptr;
            _cancel_source->reset();
            _cancel_token                 = _cancel_source->token();
            _cancellation_handler         = nullptr;
            _cancellation_observer        = nullptr;
            _legacy_cancellation_handler  = nullptr;
            _legacy_cancellation_observer = nullptr;
            _legacy_cancellation_context  = nullptr;
            _cancellation_retain          = false;
            _cancel_acknowledged          = false;
            _complete                     = false;
            _timeout_expired              = false;
            _poll_wakeup                  = false;
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
        bool canceled() const;
        bool begin_device();
        void end_device();
        void acknowledge();

        void cancellation_handler_set(cancellation_handler* handler) {
            __atomic_store_n(&_cancellation_handler, handler, __ATOMIC_RELEASE);
        }
        bool has_cancellation_handler() const {
            return __atomic_load_n(&_cancellation_handler, __ATOMIC_ACQUIRE) != nullptr ||
                   __atomic_load_n(&_legacy_cancellation_handler, __ATOMIC_ACQUIRE) !=
                       nullptr;
        }
        void cancellation_observer_set(cancellation_observer* observer) {
            __atomic_store_n(&_cancellation_observer, observer, __ATOMIC_RELEASE);
        }
        bool retain_callback_ref();
        void release_callback_ref();
        void cancel_handler_set(legacy_cancellation_handler handler) {
            __atomic_store_n(&_legacy_cancellation_handler, handler, __ATOMIC_RELEASE);
        }
        void cancel_observer_set(legacy_cancellation_observer observer, void* context) {
            __atomic_store_n(&_legacy_cancellation_context, context, __ATOMIC_RELEASE);
            __atomic_store_n(&_legacy_cancellation_observer, observer, __ATOMIC_RELEASE);
        }
        bool cancellation_retain() const {
            return _cancellation_retain;
        }
        core::cltoken cancellation_token() const {
            return _cancel_token;
        }
        bool cancellation_requested() const {
            return _cancel_token.canceled();
        }
        core::cancel_reason cancellation_reason() const {
            return _cancel_token.reason();
        }
        bool cancel_device(bool                retain = false,
                           core::cancel_reason reason = core::cancel_reason::request) {
            if (!_cancel_source->cancel(reason))
                return false;
            auto* handler = __atomic_load_n(&_cancellation_handler, __ATOMIC_ACQUIRE);
            auto  legacy_handler =
                __atomic_load_n(&_legacy_cancellation_handler, __ATOMIC_ACQUIRE);
            _cancellation_retain = retain;
            if (handler != nullptr || legacy_handler != nullptr) {
                const bool callback_ref = retain_callback_ref();
                __atomic_add_fetch(&_cancellation_callbacks, 1u, __ATOMIC_ACQ_REL);
                if (handler != nullptr)
                    handler->cancellation_requested(*this, reason, retain);
                else
                    legacy_handler(*this);
                __atomic_sub_fetch(&_cancellation_callbacks, 1u, __ATOMIC_ACQ_REL);
                ASTIA_IO_TSAN_RELEASE(const_cast<const u32*>(&_cancellation_callbacks));
                _cancellation_retain = false;
                if (callback_ref)
                    release_callback_ref();
            }
            else {
                wake();
                cancel_acknowledge();
            }
            if (!retain)
                cancel_acknowledge();
            return true;
        }
        void cancel_acknowledge();

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
        core::clsource*                 _cancel_source = nullptr;
        core::cltoken                   _cancel_token;
        cancellation_handler*           _cancellation_handler         = nullptr;
        cancellation_observer*          _cancellation_observer        = nullptr;
        legacy_cancellation_handler     _legacy_cancellation_handler  = nullptr;
        legacy_cancellation_observer    _legacy_cancellation_observer = nullptr;
        void*                           _legacy_cancellation_context  = nullptr;
        volatile u32                    _cancellation_callbacks       = 0;
        bool                            _cancellation_retain          = false;
        bool                            _cancel_acknowledged          = false;

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

        core::clsource     _cancel_source;
        astia::io::request _request;
        volatile u8        _state          = static_cast<u8>(state::owner);
        volatile u32       _refs           = 1;
        bool               _heap_owned     = false;
        bool               _owner_released = false;

        void release_ref() {
            if (__atomic_sub_fetch(&_refs, 1u, __ATOMIC_ACQ_REL) == 0 && _heap_owned)
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
            __atomic_add_fetch(&_refs, 1u, __ATOMIC_ACQ_REL);
            return true;
        }

        void release_for_device() {
            release_ref();
        }

        void release_owner() {
            bool expected = false;
            if (!__atomic_compare_exchange_n(&_owner_released, &expected, true, false,
                                             __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                return;
            const auto observed =
                static_cast<state>(__atomic_load_n(&_state, __ATOMIC_ACQUIRE));
            if (observed == state::parked || observed == state::active ||
                observed == state::active_completed) {
                request().cancel_device(true, core::cancel_reason::owner);
            }
            release_ref();
        }

        operation() : _request(_cancel_source) {
            _request._operation = this;
        }
        operation(void* buffer, u32 length, u64 offset, direction direction,
                  mm::uptr<core::completion<i64>> completion = {})
            : _request(_cancel_source, buffer, length, offset, 0, direction) {
            _request._operation = this;
            _request.completion(
                static_cast<mm::uptr<core::completion<i64>>&&>(completion));
        }
        operation(void* buffer, u32 length, u64 offset, u64 user_data,
                  direction direction, mm::uptr<core::completion<i64>> completion = {})
            : _request(_cancel_source, buffer, length, offset, user_data, direction) {
            _request._operation = this;
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
            __atomic_store_n(&_refs, 1u, __ATOMIC_RELEASE);
            __atomic_store_n(&_owner_released, false, __ATOMIC_RELEASE);
            __atomic_store_n(&_state, static_cast<u8>(state::owner), __ATOMIC_RELEASE);
        }

        bool park_device() {
            u8 expected = static_cast<u8>(state::owner);
            if (!__atomic_compare_exchange_n(&_state, &expected,
                                             static_cast<u8>(state::parked), false,
                                             __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                return false;
            __atomic_add_fetch(&_refs, 1u, __ATOMIC_ACQ_REL);
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
                if (current == state::completed || current == state::owner ||
                    current == state::active_completed)
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

        bool cancellation_acknowledged() const {
            return __atomic_load_n(&_request._cancel_acknowledged, __ATOMIC_ACQUIRE);
        }
    };

    inline void request::cancel_acknowledge() {
        if (!cancellation_requested())
            return;
        bool expected = false;
        if (!__atomic_compare_exchange_n(&_cancel_acknowledged, &expected, true, false,
                                         __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return;
        auto* observer =
            __atomic_exchange_n(&_cancellation_observer, nullptr, __ATOMIC_ACQ_REL);
        if (observer != nullptr)
            observer->cancellation_acknowledged(*this);
        else {
            auto  legacy_observer = __atomic_exchange_n(&_legacy_cancellation_observer,
                                                        nullptr, __ATOMIC_ACQ_REL);
            auto* context =
                __atomic_load_n(&_legacy_cancellation_context, __ATOMIC_ACQUIRE);
            if (legacy_observer != nullptr)
                legacy_observer(*this, context);
        }
        if (_operation != nullptr)
            _operation->acknowledge();
    }

    inline bool request::retain_callback_ref() {
        return _operation != nullptr && _operation->retain_for_device();
    }

    inline void request::release_callback_ref() {
        if (_operation != nullptr)
            _operation->release_for_device();
    }

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

    inline bool request::canceled() const {
        return _operation != nullptr &&
               (_operation->canceled() || cancellation_requested());
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
