#pragma once

#include <astia/api/aio.h>

#include <type_traits>

#include <concepts>
#include <cstdint>
#include <span>

namespace astia::io::aio::prototype {

    enum class opcode : std::uint32_t {
        read   = OP_READ,
        cancel = OP_CANCEL,
    };

    struct operation_id {
        std::uint32_t slot;
        std::uint64_t generation;
    };

    struct user_data {
        std::uint64_t value;
    };

    enum class syscall_number : std::uint64_t {};

    enum class dispatch_result : std::uint8_t {
        handled,
        cancelled,
        unknown_opcode,
    };

    template <typename Impl>
    concept handler_implementation =
        requires(typename Impl::context_type& context, const astia_sqe& sqe) {
            { Impl::handle(context, sqe) } noexcept -> std::same_as<dispatch_result>;
        };

    template <handler_implementation Impl, opcode Opcode>
    struct handler {
        static constexpr opcode opcode_value = Opcode;

        static dispatch_result invoke(void* context, const astia_sqe& sqe) noexcept {
            return Impl::handle(*static_cast<typename Impl::context_type*>(context), sqe);
        }
    };

    struct descriptor {
        opcode operation;
        dispatch_result (*invoke)(void*, const astia_sqe&) noexcept;
    };

    template <typename Handler>
    constexpr descriptor describe() noexcept {
        return {Handler::opcode_value, &Handler::invoke};
    }

    inline dispatch_result dispatch(std::span<const descriptor> handlers, void* context,
                                    const astia_sqe& sqe) noexcept {
        const auto operation = static_cast<opcode>(sqe.op);
        for (const descriptor& candidate : handlers) {
            if (candidate.operation == operation)
                return candidate.invoke(context, sqe);
        }
        return dispatch_result::unknown_opcode;
    }

    static_assert(!std::is_same_v<opcode, syscall_number>);
    static_assert(!std::is_same_v<opcode, operation_id>);
    static_assert(!std::is_same_v<opcode, user_data>);
    static_assert(std::is_trivially_copyable_v<descriptor>);

} // namespace astia::io::aio::prototype
