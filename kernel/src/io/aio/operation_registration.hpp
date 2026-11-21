#pragma once

#include "dispatch.hpp"
#include "filesystem.hpp"
#include "handler.hpp"
#include "readiness.hpp"
#include "socket.hpp"

#define ASTIA_AIO_REGISTER_CORE(Name, Opcode)                                 \
    namespace astia::io::aio::operations {                                    \
        struct operation_##Name : handler<operation_##Name, opcode::Opcode> { \
            static bool handle(ring::ring_slot& slot, astia_rings* ring,      \
                               const astia_sqe& sqe) noexcept {               \
                return dispatch::dispatch_core(slot, ring, sqe);              \
            }                                                                 \
        };                                                                    \
        [[gnu::used, gnu::section("astia_aio_handlers")]]                     \
        const descriptor descriptor_##Name = operation_##Name::describe();    \
    }

#define ASTIA_AIO_REGISTER_FILESYSTEM(Name, Opcode)                           \
    namespace astia::io::aio::operations {                                    \
        struct operation_##Name : handler<operation_##Name, opcode::Opcode> { \
            static bool handle(ring::ring_slot& slot, astia_rings* ring,      \
                               const astia_sqe& sqe) noexcept {               \
                return filesystem::dispatch(slot, ring, sqe);                 \
            }                                                                 \
        };                                                                    \
        [[gnu::used, gnu::section("astia_aio_handlers")]]                     \
        const descriptor descriptor_##Name = operation_##Name::describe();    \
    }

#define ASTIA_AIO_REGISTER_READINESS(Name, Opcode)                            \
    namespace astia::io::aio::operations {                                    \
        struct operation_##Name : handler<operation_##Name, opcode::Opcode> { \
            static bool handle(ring::ring_slot& slot, astia_rings* ring,      \
                               const astia_sqe& sqe) noexcept {               \
                return readiness::dispatch_registered(slot, ring, sqe);       \
            }                                                                 \
        };                                                                    \
        [[gnu::used, gnu::section("astia_aio_handlers")]]                     \
        const descriptor descriptor_##Name = operation_##Name::describe();    \
    }

#define ASTIA_AIO_REGISTER_SOCKET(Name, Opcode)                               \
    namespace astia::io::aio::operations {                                    \
        struct operation_##Name : handler<operation_##Name, opcode::Opcode> { \
            static bool handle(ring::ring_slot& slot, astia_rings* ring,      \
                               const astia_sqe& sqe) noexcept {               \
                return socket::dispatch(slot, ring, sqe);                     \
            }                                                                 \
        };                                                                    \
        [[gnu::used, gnu::section("astia_aio_handlers")]]                     \
        const descriptor descriptor_##Name = operation_##Name::describe();    \
    }
