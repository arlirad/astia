#pragma once

#include "dispatch.hpp"
#include "handler.hpp"
#include "ring.hpp"

namespace astia::io::aio::readiness {

    constexpr bool handles(u32 op) {
        return op == OP_POLL_ADD || op == OP_SLEEP;
    }

    bool dispatch(ring::ring_slot& slot, astia_rings* ring, const astia_sqe& sqe,
                  const mm::shptr<fs::open_file>& object);

    template <opcode Opcode>
    struct operation : handler<operation<Opcode>, Opcode> {
        static_assert(handles(static_cast<u32>(Opcode)));

        static bool handle(ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            if constexpr (Opcode == opcode::poll_add)
                return dispatch(slot, ring, sqe, dispatch::file_for(sqe.fd));
            else
                return dispatch(slot, ring, sqe, {});
        }
    };

} // namespace astia::io::aio::readiness
