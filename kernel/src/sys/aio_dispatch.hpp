#pragma once

#include "aio_ring.hpp"

namespace astia::sys::aio_dispatch {

    bool dispatch_one(aio_ring::ring_slot& slot, astia_rings* ring,
                      const astia_sqe& sqe);
    void retry_pending(aio_ring::ring_slot& slot, astia_rings* ring);
    void resume_parked(astia_rings* ring, aio_ring::parked_op& op);
    void complete_control(proc::process* owner, astia_rings* ring,
                          aio_ring::parked_op& op, i32 result);
    u64 poll_now_ns();

} // namespace astia::sys::aio_dispatch
