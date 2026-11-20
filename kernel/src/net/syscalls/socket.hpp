#pragma once

#include "socket_operation.hpp"

namespace astia::net::syscalls {

    template <typename Context>
    using socket = socket_operation<Context, ::astia::io::aio::prototype::opcode::socket>;

} // namespace astia::net::syscalls
