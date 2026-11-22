#pragma once

#include <cstdint>

namespace astia::io::aio {

    struct operation_id {
        std::uint32_t slot;
        std::uint64_t generation;

        friend constexpr bool operator==(operation_id, operation_id) = default;
    };

    enum class operation_state : std::uint8_t {
        pending,
        terminal,
    };

    class operation {
        public:
        explicit constexpr operation(operation_id id) : _id(id) {}

        constexpr operation_id id() const {
            return _id;
        }

        constexpr operation_state state() const {
            return _state;
        }

        constexpr bool cancellation_requested() const {
            return _cancellation_requested;
        }

        constexpr bool completed() const {
            return _completed;
        }

        constexpr bool acknowledged() const {
            return _acknowledged;
        }

        constexpr bool device_acknowledged() const {
            return _device_acknowledged;
        }

        constexpr bool quiescent() const {
            return _quiescent;
        }

        constexpr bool request_cancel() {
            if (_state == operation_state::terminal)
                return false;
            _cancellation_requested = true;
            return true;
        }

        constexpr bool complete(std::int32_t result) {
            if (_state == operation_state::terminal)
                return false;
            _result    = result;
            _completed = true;
            _state     = operation_state::terminal;
            return true;
        }

        constexpr std::int32_t result() const {
            return _result;
        }

        constexpr bool acknowledge_device() {
            if (_state != operation_state::terminal || _device_acknowledged)
                return false;
            _device_acknowledged = true;
            return true;
        }

        constexpr bool mark_quiescent() {
            if (!_device_acknowledged || _quiescent)
                return false;
            _quiescent = true;
            return true;
        }

        constexpr bool acknowledge() {
            if (!_quiescent || _acknowledged)
                return false;
            _acknowledged = true;
            return true;
        }

        private:
        operation_id    _id;
        operation_state _state                  = operation_state::pending;
        std::int32_t    _result                 = 0;
        bool            _cancellation_requested = false;
        bool            _completed              = false;
        bool            _device_acknowledged    = false;
        bool            _quiescent              = false;
        bool            _acknowledged           = false;
    };

} // namespace astia::io::aio
