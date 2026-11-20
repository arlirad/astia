#pragma once

#include "dispatch.hpp"
#include "handler.hpp"

namespace astia::io::aio::core {

#define ASTIA_AIO_CORE_HANDLER(Name, Opcode)                         \
    struct Name : handler<Name, Opcode> {                            \
        static bool handle(ring::ring_slot& slot, astia_rings* ring, \
                           const astia_sqe& sqe) noexcept {          \
            return dispatch::dispatch_core(slot, ring, sqe);         \
        }                                                            \
    }

    ASTIA_AIO_CORE_HANDLER(nop, opcode::nop);
    ASTIA_AIO_CORE_HANDLER(read, opcode::read);
    ASTIA_AIO_CORE_HANDLER(write, opcode::write);
    ASTIA_AIO_CORE_HANDLER(readv, opcode::readv);
    ASTIA_AIO_CORE_HANDLER(writev, opcode::writev);
    ASTIA_AIO_CORE_HANDLER(send, opcode::send);
    ASTIA_AIO_CORE_HANDLER(recv, opcode::recv);
    ASTIA_AIO_CORE_HANDLER(wait, opcode::wait);
    ASTIA_AIO_CORE_HANDLER(spawn, opcode::spawn);
    ASTIA_AIO_CORE_HANDLER(dup, opcode::dup);
    ASTIA_AIO_CORE_HANDLER(dup2, opcode::dup2);
    ASTIA_AIO_CORE_HANDLER(ioctl, opcode::ioctl);
    ASTIA_AIO_CORE_HANDLER(fcntl, opcode::fcntl);
    ASTIA_AIO_CORE_HANDLER(eventfd_create, opcode::eventfd_create);
    ASTIA_AIO_CORE_HANDLER(cancel, opcode::cancel);

#undef ASTIA_AIO_CORE_HANDLER

} // namespace astia::io::aio::core
