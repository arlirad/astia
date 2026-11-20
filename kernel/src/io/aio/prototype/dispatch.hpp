#pragma once

#include <astia/api/aio.h>

#include <type_traits>

#include <concepts>
#include <cstdint>
#include <span>

namespace astia::io::aio::prototype {

    enum class opcode : std::uint32_t {
        read       = OP_READ,
        cancel     = OP_CANCEL,
        socket     = OP_SOCKET,
        bind       = OP_BIND,
        connect    = OP_CONNECT,
        listen     = OP_LISTEN,
        accept     = OP_ACCEPT,
        sendto     = OP_SENDTO,
        recvfrom   = OP_RECVFROM,
        socketpair = OP_SOCKETPAIR,
        shutdown   = OP_SHUTDOWN,
        getsockopt = OP_GETSOCKOPT,
    };

    enum class dispatch_result : std::uint8_t {
        handled,
        pending,
        cancelled,
        unknown_opcode,
        duplicate_opcode,
    };

    template <typename Impl>
    concept handler_implementation =
        requires(typename Impl::context_type& context, const astia_sqe& sqe) {
            { Impl::handle(context, sqe) } noexcept -> std::same_as<dispatch_result>;
        };

    struct descriptor {
        opcode operation;
        dispatch_result (*invoke)(void*, const astia_sqe&) noexcept;
    };

    template <typename Impl, opcode Opcode>
    struct handler {
        static constexpr opcode opcode_value = Opcode;

        static dispatch_result dispatch(void* context, const astia_sqe& sqe) noexcept {
            static_assert(handler_implementation<Impl>);
            static_assert(std::is_empty_v<Impl>);
            return Impl::handle(*static_cast<typename Impl::context_type*>(context), sqe);
        }

        static constexpr descriptor describe() noexcept {
            static_assert(handler_implementation<Impl>);
            static_assert(std::is_empty_v<Impl>);
            return {Opcode, &handler::dispatch};
        }
    };

    inline dispatch_result dispatch(std::span<const descriptor> handlers, void* context,
                                    const astia_sqe& sqe) noexcept {
        const auto        operation = static_cast<opcode>(sqe.op);
        const descriptor* match     = nullptr;
        for (const descriptor& candidate : handlers) {
            if (candidate.operation != operation)
                continue;
            if (match != nullptr)
                return dispatch_result::duplicate_opcode;
            match = &candidate;
        }
        return match == nullptr ? dispatch_result::unknown_opcode
                                : match->invoke(context, sqe);
    }

} // namespace astia::io::aio::prototype
