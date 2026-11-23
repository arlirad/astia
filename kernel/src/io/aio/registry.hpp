#pragma once

#include "handler.hpp"

namespace astia::io::aio::registry {

    const operation_descriptor* entries();
    usize                       size();
    bool                        valid();
    /// Runs the handler for one SQE and returns whether the SQE was consumed.
    /// Handlers are coroutines; they are driven to completion inline here
    /// because the ring lock is held for the duration of a submit batch.
    bool dispatch(ring::ring_slot& slot, astia_rings* ring, const astia_sqe& sqe);

} // namespace astia::io::aio::registry
