#pragma once

#include "../../io/aio/prototype/dispatch.hpp"

namespace astia::net::syscalls {

    template <typename Context, ::astia::io::aio::prototype::opcode Opcode>
    struct socket_operation
        : ::astia::io::aio::prototype::handler<socket_operation<Context, Opcode>,
                                               Opcode> {
        using context_type = Context;

        static ::astia::io::aio::prototype::dispatch_result
        handle(context_type& context, const astia_sqe& sqe) noexcept {
            return context.socket_operation(Opcode, sqe);
        }
    };

} // namespace astia::net::syscalls
