#include <astia/atomic.hpp>

#include "../runner.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {
    struct cancel : handler<cancel, opcode::cancel> {
        static core::task<aio_result>
        invoke([[maybe_unused]] ring::ring_slot& slot, [[maybe_unused]] astia_rings* ring,
               astia_sqe sqe, [[maybe_unused]] core::cltoken token) noexcept {
            const u64 target = sqe.offset;

            auto has_capacity = [ring](u32 count) {
                const astia_cq_ring* cq = astia_rings_cq(ring);

                const u32 head =
                    astia::atomic_ref(cq->head).load(astia::memory_order::acquire);
                const u32 tail =
                    astia::atomic_ref(cq->tail).load(astia::memory_order::relaxed);

                return tail - head <= cq->entries - count;
            };

            for (u32 i = 0; i < parked_operation_slots; ++i) {
                parked_op& p = slot.park[i];

                if (p.used && p.type == parked_op::kind::operation && !p.done &&
                    p.operation != nullptr &&
                    p.operation->request().user_data() == target) {
                    // Cancellation needs two CQ slots.  Do not consume the
                    // SQE if either completion could be lost.
                    if (!has_capacity(2))
                        co_return aio_result::retry;

                    if (!cq_post(ring, target, -ECANCELED))
                        co_return aio_result::retry;

                    if (p.cancel)
                        (void) p.cancel->cancel(astia::core::cancel_reason::request);

                    (void) p.operation->request().cancel_device(
                        false, astia::core::cancel_reason::request);
                    p.operation->request().acknowledge();

                    p.done = true;

                    co_return cq_post(ring, sqe.user_data, 0) ? aio_result::pending
                                                              : aio_result::retry;
                }

                if (p.used && p.type == parked_op::kind::oom_completion && !p.done &&
                    p.pending_cqe && p.pending_user_data == target) {
                    if (!has_capacity(2))
                        co_return aio_result::retry;

                    if (!publish_pending(ring, p))
                        co_return aio_result::retry;

                    co_return cq_post(ring, sqe.user_data, 0) ? aio_result::pending
                                                              : aio_result::retry;
                }
            }

            for (runner::detached_state* entry = slot.detached_head; entry != nullptr;
                 entry                         = entry->next) {
                if (entry->user_data != target || entry->cancel.get() == token.source())
                    continue;

                // The target publishes its own -ECANCELED when it finishes;
                // this SQE only needs its own completion slot.
                if (!has_capacity(1))
                    co_return aio_result::retry;

                entry->cancel->cancel(astia::core::cancel_reason::request);

                co_return cq_post(ring, sqe.user_data, 0) ? aio_result::pending
                                                          : aio_result::retry;
            }

            co_return -ENOENT;
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor cancel_descriptor = cancel::descriptor;
} // namespace astia::io::aio::operations
