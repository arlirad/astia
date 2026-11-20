#pragma once

#include "handler.hpp"
#include "ring.hpp"

namespace astia::io::aio::filesystem {

    constexpr bool handles(u32 op) {
        switch (op) {
        case OP_OPENAT:
        case OP_CLOSE:
        case OP_STATX:
        case OP_FSYNC:
        case OP_UNLINKAT:
        case OP_MKDIRAT:
        case OP_RENAMEAT:
        case OP_READDIR:
        case OP_CHMOD:
        case OP_UTIMENSAT:
        case OP_SYMLINKAT:
        case OP_READLINKAT:
        case OP_CHOWN:
        case OP_LINKAT:
        case OP_FTRUNCATE:
            return true;
        default:
            return false;
        }
    }

    bool dispatch(ring::ring_slot& slot, astia_rings* ring, const astia_sqe& sqe);

    template <opcode Opcode>
    struct operation : handler<operation<Opcode>, Opcode> {
        static_assert(handles(static_cast<u32>(Opcode)));

        static bool handle(ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return dispatch(slot, ring, sqe);
        }
    };

} // namespace astia::io::aio::filesystem
