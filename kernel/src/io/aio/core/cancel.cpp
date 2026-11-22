#include "common.hpp"

namespace astia::io::aio::operations {

    struct cancel : handler<cancel, opcode::cancel> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            const u64 target       = sqe.offset;
            auto      has_capacity = [ring](u32 count) {
                const astia_cq_ring* cq = astia_rings_cq(ring);
                const u32            head = __atomic_load_n(&cq->head, __ATOMIC_ACQUIRE);
                const u32            tail = __atomic_load_n(&cq->tail, __ATOMIC_RELAXED);
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
                        return false;
                    if (!cq_post(ring, target, -ECANCELED))
                        return false;
                    p.operation->request().cancel_device(false);
                    p.operation->request().acknowledge();
                    p.done = true;
                    return cq_post(ring, sqe.user_data, 0);
                }
                if (p.used && p.type == parked_op::kind::oom_completion && !p.done &&
                    p.pending_cqe && p.pending_user_data == target) {
                    if (!has_capacity(2))
                        return false;
                    if (!publish_pending(ring, p))
                        return false;
                    return cq_post(ring, sqe.user_data, 0);
                }
            }
            return cq_post_guarded(ring, sqe.user_data, -ENOENT);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor cancel_descriptor = cancel::descriptor;

} // namespace astia::io::aio::operations
