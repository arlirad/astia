#pragma once

#include "handler.hpp"

namespace astia::io::aio::registry {
    const operation_descriptor* entries();
    usz                         size();
    bool                        valid();
    /// Runs the handler for one SQE and returns whether the SQE was consumed.
    bool dispatch(ring::ring_slot& slot, astia_rings* ring, const astia_sqe& sqe);
} // namespace astia::io::aio::registry
