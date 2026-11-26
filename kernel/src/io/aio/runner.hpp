#pragma once

#include <astia/atomic.hpp>
#include <astia/core/cancellation.hpp>
#include <astia/mm/share.hpp>
#include <astia/mm/shptr.hpp>

#include "handler.hpp"

namespace astia::io::aio::runner {

    /// One in-flight detached operation: its cancellation child, the inline
    /// "consumed" handshake, and the slot link used to find it from io_cancel.
    struct detached_state {
        explicit detached_state(core::clsource& root)
            : cancel(mm::share<core::clsource>(root)) {}

        /// Shared with parked operations that outlive this state: the state
        /// itself is released as soon as the operation parks, but a parked
        /// operation still has to observe cancellation of this token.
        mm::shptr<core::clsource> cancel;
        astia::atomic<bool>       consumed{true};
        u64                       user_data = 0;
        detached_state*           next      = nullptr;
    };

    /// Create an operation state whose cancellation source is a child of the
    /// ring owner's process root.  Returns an empty pointer on allocation
    /// failure.
    mm::shptr<detached_state> make_state(ring::ring_slot& slot, u64 user_data);

    core::cltoken token(const detached_state* state);

    /// Keep the cancellation source that backs `token` alive for a parked
    /// operation.  Returns an empty pointer when the token does not belong to
    /// a runner state (for example a caller-owned source in a host test).
    mm::shptr<core::clsource> retain_source(ring::ring_slot&     slot,
                                            const core::cltoken& token);

    /// Adopt an operation: run it inline, register it in the slot while it is
    /// in flight, and publish its CQE when the continuation finishes.  The
    /// caller must hold the ring lock.
    bool launch(core::task<aio_result> operation, mm::shptr<detached_state> state,
                ring::ring_slot& slot, astia_rings* ring, const astia_sqe& sqe);

} // namespace astia::io::aio::runner
