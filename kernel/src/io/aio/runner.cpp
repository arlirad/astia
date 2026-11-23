#include "runner.hpp"

#include <astia/atomic.hpp>
#include <astia/core/det_task.hpp>
#include <astia/mm/shptr.hpp>

#include "ring.hpp"

#include <new>

namespace astia::io::aio::runner {

    namespace {
        /// Shared between the submitter and the driver: reports whether the
        /// SQE was consumed and keeps the state alive until both are done.
        struct detached_state {
            astia::atomic<bool> consumed{true};
        };

        core::det_task run_detached(core::task<aio_result> operation,
                                    ring::ring_slot& slot, astia_rings* ring,
                                    astia_sqe sqe, mm::shptr<detached_state> state) {
            slot.detached_ops.fetch_add(1, astia::memory_order::acq_rel);

            auto completed = co_await static_cast<core::task<aio_result>&&>(operation);
            const aio_result result =
                completed ? completed.value : aio_result(completed.error);

            bool consumed = true;
            {
                // The driver resumes outside the submit batch, so the ring is
                // re-locked only for publication and the in-flight release.
                ring::ring_guard guard(slot.lock);
                if (result.kind() == aio_result::state::complete)
                    consumed = ring::cq_post(ring, sqe.user_data, result.value());

                slot.detached_ops.fetch_sub(1, astia::memory_order::acq_rel);
            }

            state->consumed.store(consumed && result.kind() != aio_result::state::retry,
                                  astia::memory_order::release);
        }
    } // namespace

    bool launch(core::task<aio_result> operation, ring::ring_slot& slot,
                astia_rings* ring, const astia_sqe& sqe) {
        if (!operation.valid())
            return false;

        auto state = mm::shptr<detached_state>(new (std::nothrow) detached_state{});
        if (!state)
            return false;

        const core::det_task driver = run_detached(
            static_cast<core::task<aio_result>&&>(operation), slot, ring, sqe, state);
        if (!driver.started)
            return false;

        return state->consumed.load(astia::memory_order::acquire);
    }

} // namespace astia::io::aio::runner
