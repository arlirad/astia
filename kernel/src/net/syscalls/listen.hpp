#pragma once

#include "socket_operation.hpp"

namespace astia::net::syscalls {

    template <typename Context>
    using listen = socket_operation<Context, ::astia::io::aio::prototype::opcode::listen>;

} // namespace astia::net::syscalls
