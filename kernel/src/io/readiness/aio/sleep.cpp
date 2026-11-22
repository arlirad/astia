#include "../../aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    namespace {

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

    struct sleep : handler<sleep, opcode::sleep> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return handle_sleep(slot, ring, sqe);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor sleep_descriptor = sleep::descriptor;

} // namespace astia::io::aio::operations
