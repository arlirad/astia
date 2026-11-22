#include "dispatch.hpp"

#include <astia/api/errno.h>
#include <astia/io.hpp>

#include "../aio/dispatch.hpp"
#include "../aio/memory.hpp"

namespace astia::io::aio::readiness {

    using namespace ring;

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

        bool handle_sleep(ring_slot& slot, astia_rings* ring, const astia_sqe& s) {
            if (s.fd != -1 ||
                (s.address == 0 ? s.length != 0 : s.length != sizeof(u64)) ||
                (s.op_flags & ~SLEEP_ABSOLUTE) != 0 || s.aux != 0)
                return cq_post_guarded(ring, s.user_data, -EINVAL);
            if (s.address != 0 &&
                !memory::user_span_ok_for(span<u8>::bytes(s.address, sizeof(u64))))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            if (arch::timer_hz() == 0)
                return cq_post_guarded(ring, s.user_data, -EIO);
            const bool absolute = (s.op_flags & SLEEP_ABSOLUTE) != 0;
            const u64  now      = dispatch::poll_now_ns();
            if ((!absolute && s.offset == 0) || (absolute && s.offset <= now))
                return cq_post_guarded(ring, s.user_data, 0);

            parked_op* p = nullptr;
            for (u32 i = 0; i < parked_operation_slots; ++i) {
                if (!slot.park[i].used) {
                    p = &slot.park[i];
                    break;
                }
            }
            if (p == nullptr)
                return cq_post_guarded(ring, s.user_data, -EBUSY);

            p->used                 = true;
            p->type                 = parked_op::kind::operation;
            p->done                 = false;
            p->resuming             = false;
            p->control              = true;
            p->signal_interruptible = true;
            p->sleeping             = true;
            p->sleep_absolute       = absolute;
            p->sleep_deadline =
                absolute ? s.offset
                         : (UINT64_MAX - now < s.offset ? UINT64_MAX : now + s.offset);
            p->sleep_remaining_user = absolute ? 0 : s.address;
            p->fd                   = -1;
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
            return true;
        }

    } // namespace

    bool dispatch(ring_slot& slot, astia_rings* ring, const astia_sqe& sqe,
                  const mm::shptr<fs::open_file>& object) {
        switch (sqe.op) {
        case OP_POLL_ADD:
            return handle_poll_add(slot, ring, sqe, object);
        case OP_SLEEP:
            return handle_sleep(slot, ring, sqe);
        default:
            return false;
        }
    }

    bool dispatch_registered(ring_slot& slot, astia_rings* ring, const astia_sqe& sqe) {
        if (sqe.op == OP_POLL_ADD)
            return dispatch(slot, ring, sqe, dispatch::file_for(sqe.fd));
        return dispatch(slot, ring, sqe, {});
    }

} // namespace astia::io::aio::readiness
