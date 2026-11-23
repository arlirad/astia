#include "runner.hpp"

#include <astia/api/errno.h>
#include <astia/core/det_task.hpp>

#include "ring.hpp"

#include <new>

namespace astia::io::aio::runner {

    namespace {
        void unlink(ring::ring_slot& slot, detached_state* state) {
            if (slot.detached_head == state) {
                slot.detached_head = state->next;
                return;
            }
            for (detached_state* entry = slot.detached_head; entry != nullptr;
                 entry                 = entry->next) {
                if (entry->next == state) {
                    entry->next = state->next;
                    return;
                }
            }
        }
    } // namespace

    mm::shptr<detached_state> make_state(ring::ring_slot& slot, u64 user_data) {
        // Owner-less rings exist only in host tests; they share the same
        // never-cancelled source as default-constructed tokens.
        auto state = mm::shptr<detached_state>(new (std::nothrow) detached_state(
            slot.owner ? slot.owner->cancellation_root() : core::unrestricted_source));
        if (!state)
            return {};
        state->user_data = user_data;
        return state;
    }

    core::cltoken token(const detached_state* state) {
        return state == nullptr ? core::cltoken{} : state->cancel.token();
    }

    bool launch(core::task<aio_result> operation, mm::shptr<detached_state> state,
                ring::ring_slot& slot, astia_rings* ring, const astia_sqe& sqe) {
        if (!operation.valid() || !state)
            return false;

        // The submit batch holds the ring lock, so registration is serialized
        // against io_cancel and io_destroy.
        state->next        = slot.detached_head;
        slot.detached_head = state.get();
        slot.detached_ops.fetch_add(1, astia::memory_order::acq_rel);

        const core::det_task driver = core::det_task::then(
            static_cast<core::task<aio_result>&&>(operation),
            [state, slot_ptr = &slot, ring, sqe](optional<aio_result> completed) {
                aio_result result =
                    completed ? completed.value : aio_result(completed.error);
                const core::cltoken token = state->cancel.token();
                if (token.canceled())
                    result = aio_result(-ECANCELED);
                bool consumed = true;
                {
                    // The continuation resumes outside the submit batch, so
                    // the ring is re-locked only for publication.
                    ring::ring_guard guard(slot_ptr->lock);
                    unlink(*slot_ptr, state.get());
                    if (result.kind() == aio_result::state::complete)
                        consumed = ring::cq_post(ring, sqe.user_data, result.value());
                    slot_ptr->detached_ops.fetch_sub(1, astia::memory_order::acq_rel);
                }
                state->consumed.store(consumed &&
                                          result.kind() != aio_result::state::retry,
                                      astia::memory_order::release);
            });
        if (!driver.started) {
            // launch runs under the ring lock, so the rollback needs no guard.
            unlink(slot, state.get());
            slot.detached_ops.fetch_sub(1, astia::memory_order::acq_rel);
            return false;
        }
        return state->consumed.load(astia::memory_order::acquire);
    }

} // namespace astia::io::aio::runner
