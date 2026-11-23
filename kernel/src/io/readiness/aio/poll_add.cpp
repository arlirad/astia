#include "../../aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    struct poll_add : handler<poll_add, opcode::poll_add> {
        static core::task<aio_result> invoke([[maybe_unused]] ring::ring_slot& slot,
                                             [[maybe_unused]] astia_rings*     ring,
                                             astia_sqe sqe) noexcept {
            const mm::shptr<fs::open_file> object = dispatch::file_for(sqe.fd);
            if (!object)
                co_return POLLNVAL;
            const u64 generation = ready_generation();
            const u32 ready      = object->poll(sqe.aux);
            if (ready != 0)
                co_return static_cast<i32>(ready);
            parked_op* p          = nullptr;
            u32        park_index = 0;
            for (u32 i = 0; i < parked_operation_slots; ++i)
                if (!slot.park[i].used) {
                    p          = &slot.park[i];
                    park_index = i;
                    break;
                }
            if (p == nullptr)
                co_return -EBUSY;
            p->used                 = true;
            p->type                 = parked_op::kind::operation;
            p->done                 = false;
            p->resuming             = false;
            p->control              = true;
            p->signal_interruptible = true;
            p->readiness            = true;
            p->poll_events          = sqe.aux;
            p->fd                   = sqe.fd;
            p->object               = object;
            p->operation = new ::astia::io::operation(nullptr, 0, 0, sqe.user_data,
                                                      ::astia::io::direction::input);
            if (p->operation == nullptr) {
                p->type = parked_op::kind::oom_completion;
                if (cq_post_guarded(ring, sqe.user_data, -ENOMEM))
                    p->done = true;
                else {
                    p->pending_cqe       = true;
                    p->pending_user_data = sqe.user_data;
                    p->pending_result    = -ENOMEM;
                }
                co_return aio_result::pending;
            }
            p->operation->heap_owned(true);
            publish_waiter_hint(slot, park_index, object->node().get());
            if (ready_generation() != generation)
                dispatch::resume_parked(proc::current_process(), ring, *p);
            co_return aio_result::pending;
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor poll_add_descriptor = poll_add::descriptor;

} // namespace astia::io::aio::operations
