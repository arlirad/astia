#include "../../aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    namespace {

        bool handle_poll_add(ring_slot& slot, astia_rings* ring, const astia_sqe& s,
                             const mm::shptr<fs::open_file>& object) {
            if (!object)
                return cq_post_guarded(ring, s.user_data, POLLNVAL);
            const u64 generation = ready_generation();
            const u32 ready      = object->poll(s.aux);
            if (ready != 0)
                return cq_post_guarded(ring, s.user_data, static_cast<i32>(ready));
            parked_op* p          = nullptr;
            u32        park_index = 0;
            for (u32 i = 0; i < parked_operation_slots; ++i)
                if (!slot.park[i].used) {
                    p          = &slot.park[i];
                    park_index = i;
                    break;
                }
            if (p == nullptr)
                return cq_post_guarded(ring, s.user_data, -EBUSY);
            p->used                 = true;
            p->type                 = parked_op::kind::operation;
            p->done                 = false;
            p->resuming             = false;
            p->control              = true;
            p->signal_interruptible = true;
            p->readiness            = true;
            p->poll_events          = s.aux;
            p->fd                   = s.fd;
            p->object               = object;
            p->operation = new ::astia::io::operation(nullptr, 0, 0, s.user_data,
                                                      ::astia::io::direction::input);
            if (p->operation == nullptr) {
                p->type = parked_op::kind::oom_completion;
                if (cq_post_guarded(ring, s.user_data, -ENOMEM))
                    p->done = true;
                else {
                    p->pending_cqe       = true;
                    p->pending_user_data = s.user_data;
                    p->pending_result    = -ENOMEM;
                }
                return true;
            }
            p->operation->heap_owned(true);
            publish_waiter_hint(slot, park_index, object->node().get());
            if (ready_generation() != generation)
                dispatch::resume_parked(proc::current_process(), ring, *p);
            return true;
        }

    } // namespace

    struct poll_add : handler<poll_add, opcode::poll_add> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return handle_poll_add(slot, ring, sqe, dispatch::file_for(sqe.fd));
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor poll_add_descriptor = poll_add::descriptor;

} // namespace astia::io::aio::operations
