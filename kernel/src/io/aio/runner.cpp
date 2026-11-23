#include "runner.hpp"

#include <astia/atomic.hpp>
#include <astia/core/det_task.hpp>
#include <astia/mm/shptr.hpp>

#include "ring.hpp"

#include <new>

namespace astia::io::aio::runner {

    namespace {
        /// Carries the inline "consumed" answer back to launch when the
        /// continuation may run after launch has returned.
        struct detached_state {
            astia::atomic<bool> consumed{true};
        };
    } // namespace

    bool launch(core::task<aio_result> operation, ring::ring_slot& slot,
                astia_rings* ring, const astia_sqe& sqe) {
        if (!operation.valid())
            return false;
        auto state = mm::shptr<detached_state>(new (std::nothrow) detached_state{});
        if (!state)
            return false;

        slot.detached_ops.fetch_add(1, astia::memory_order::acq_rel);
        const core::det_task driver = core::det_task::then(
            static_cast<core::task<aio_result>&&>(operation),
            [state, slot_ptr = &slot, ring, sqe](optional<aio_result> completed) {
                const aio_result result =
                    completed ? completed.value : aio_result(completed.error);
                bool consumed = true;
                {
                    // The continuation resumes outside the submit batch, so
                    // the ring is re-locked only for publication.
                    ring::ring_guard guard(slot_ptr->lock);
                    if (result.kind() == aio_result::state::complete)
                        consumed = ring::cq_post(ring, sqe.user_data, result.value());
                    slot_ptr->detached_ops.fetch_sub(1, astia::memory_order::acq_rel);
                }
                state->consumed.store(consumed &&
                                          result.kind() != aio_result::state::retry,
                                      astia::memory_order::release);
            });
        if (!driver.started) {
            slot.detached_ops.fetch_sub(1, astia::memory_order::acq_rel);
            return false;
        }
        return state->consumed.load(astia::memory_order::acquire);
    }

} // namespace astia::io::aio::runner
