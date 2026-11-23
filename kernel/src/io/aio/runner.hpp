#pragma once

#include "handler.hpp"

namespace astia::io::aio::runner {

    /// Runs one operation.  The handler starts inline; if it suspends, its
    /// frame is adopted until the await completes, then the CQE is published.
    /// Returns whether the SQE was consumed.
    bool launch(core::task<aio_result> operation, ring::ring_slot& slot,
                astia_rings* ring, const astia_sqe& sqe);

} // namespace astia::io::aio::runner
