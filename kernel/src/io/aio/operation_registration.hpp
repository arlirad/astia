#pragma once

#include "dispatch.hpp"
#include "filesystem.hpp"
#include "handler.hpp"
#include "readiness.hpp"
#include "socket.hpp"

#define ASTIA_AIO_REGISTER_HANDLER(Name, Opcode, Dispatch)                    \
    namespace astia::io::aio::operations {                                    \
        struct operation_##Name : handler<operation_##Name, opcode::Opcode> { \
            static bool handle(ring::ring_slot& slot, astia_rings* ring,      \
                               const astia_sqe& sqe) noexcept {               \
                return Dispatch(slot, ring, sqe);                             \
            }                                                                 \
        };                                                                    \
        [[gnu::used, gnu::section("astia_aio_handlers")]]                     \
        const descriptor descriptor_##Name = operation_##Name::describe();    \
    }

#define ASTIA_AIO_REGISTER_CORE(Name, Opcode) \
    ASTIA_AIO_REGISTER_HANDLER(Name, Opcode, dispatch::dispatch_core)

#define ASTIA_AIO_REGISTER_FILESYSTEM(Name, Opcode) \
    ASTIA_AIO_REGISTER_HANDLER(Name, Opcode, filesystem::dispatch)

#define ASTIA_AIO_REGISTER_READINESS(Name, Opcode) \
    ASTIA_AIO_REGISTER_HANDLER(Name, Opcode, readiness::dispatch_registered)

#define ASTIA_AIO_REGISTER_SOCKET(Name, Opcode) \
    ASTIA_AIO_REGISTER_HANDLER(Name, Opcode, socket::dispatch)
