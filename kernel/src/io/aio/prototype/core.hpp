#pragma once

#include <cstdint>

namespace astia::io::aio::prototype {

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
        explicit constexpr operation(operation_id id) : id_(id) {}

        constexpr operation_id id() const {
            return id_;
        }

        constexpr operation_state state() const {
            return state_;
        }

        constexpr bool cancellation_requested() const {
            return cancellation_requested_;
        }

        constexpr bool completed() const {
            return completed_;
        }

        constexpr bool acknowledged() const {
            return acknowledged_;
        }

        constexpr bool quiescent() const {
            return quiescent_;
        }

        constexpr bool request_cancel() {
            if (state_ == operation_state::terminal)
                return false;
            cancellation_requested_ = true;
            return true;
        }

        constexpr bool complete(std::int32_t result) {
            if (state_ == operation_state::terminal)
                return false;
            result_    = result;
            completed_ = true;
            state_     = operation_state::terminal;
            return true;
        }

        constexpr std::int32_t result() const {
            return result_;
        }

        constexpr bool mark_quiescent() {
            if (state_ != operation_state::terminal || quiescent_)
                return false;
            quiescent_ = true;
            return true;
        }

        constexpr bool acknowledge() {
            if (!quiescent_ || acknowledged_)
                return false;
            acknowledged_ = true;
            return true;
        }

        private:
        operation_id    id_;
        operation_state state_                  = operation_state::pending;
        std::int32_t    result_                 = 0;
        bool            cancellation_requested_ = false;
        bool            completed_              = false;
        bool            quiescent_              = false;
        bool            acknowledged_           = false;
    };

} // namespace astia::io::aio::prototype
