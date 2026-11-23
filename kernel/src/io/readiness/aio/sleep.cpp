#include "../../aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    struct sleep : handler<sleep, opcode::sleep> {
        static core::task<aio_result> invoke([[maybe_unused]] ring::ring_slot& slot,
                                             [[maybe_unused]] astia_rings*     ring,
                                             astia_sqe sqe) noexcept {
            if (sqe.fd != -1 ||
                (sqe.address == 0 ? sqe.length != 0 : sqe.length != sizeof(u64)) ||
                (sqe.op_flags & ~SLEEP_ABSOLUTE) != 0 || sqe.aux != 0)
                co_return -EINVAL;
            if (sqe.address != 0 &&
                !memory::user_span_ok_for(span<u8>::bytes(sqe.address, sizeof(u64))))
                co_return -EFAULT;
            if (arch::timer_hz() == 0)
                co_return -EIO;
            const bool absolute = (sqe.op_flags & SLEEP_ABSOLUTE) != 0;
            const u64  now      = dispatch::poll_now_ns();
            if ((!absolute && sqe.offset == 0) || (absolute && sqe.offset <= now))
                co_return 0;

            parked_op* p = nullptr;
            for (u32 i = 0; i < parked_operation_slots; ++i) {
                if (!slot.park[i].used) {
                    p = &slot.park[i];
                    break;
                }
            }
            if (p == nullptr)
                co_return -EBUSY;

            p->used                 = true;
            p->type                 = parked_op::kind::operation;
            p->done                 = false;
            p->resuming             = false;
            p->control              = true;
            p->signal_interruptible = true;
            p->sleeping             = true;
            p->sleep_absolute       = absolute;
            p->sleep_deadline =
                absolute
                    ? sqe.offset
                    : (UINT64_MAX - now < sqe.offset ? UINT64_MAX : now + sqe.offset);
            p->sleep_remaining_user = absolute ? 0 : sqe.address;
            p->fd                   = -1;
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
            co_return aio_result::pending;
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor sleep_descriptor = sleep::descriptor;

} // namespace astia::io::aio::operations
