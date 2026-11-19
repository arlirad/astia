#include "sigaltstack.hpp"

#include "../../proc.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"
#include <astia/api/errno.h>
#include <astia/api/signal.h>

namespace astia::proc::syscall {

    i64 sigaltstack(u64 stack_ptr, u64 old_stack_ptr) {
        auto* self = current_thread();
        if (self == nullptr)
            return -ESRCH;

        optional<sys::validated_user_ptr<astia_stack_t>> old_range;
        astia_stack_t                                    next{};
        astia_stack_t                                    old{};
        if (stack_ptr != 0) {
            auto validated = sys::user_ptr<astia_stack_t>(stack_ptr).validate();
            if (!validated || !sys::copy_in(&next, validated.value.bytes()))
                return -EFAULT;
            if (next.flags != 0 && next.flags != ASTIA_SS_DISABLE)
                return -EINVAL;
            if (next.flags == 0) {
                if (next.size < ASTIA_MINSIGSTKSZ || next.sp + next.size < next.sp)
                    return -ENOMEM;
                if (!sys::user_span_ok(span<u8>::bytes(next.sp, next.size)))
                    return -EFAULT;
            }
        }
        if (old_stack_ptr != 0) {
            auto validated = sys::user_ptr<astia_stack_t>(old_stack_ptr).validate();
            if (!validated)
                return -EFAULT;
            old_range = validated.value;
        }
        old.sp   = self->signal_stack_sp();
        old.size = self->signal_stack_size();
        old.flags =
            self->signal_stack_on_alt() ? ASTIA_SS_ONSTACK : self->signal_stack_flags();
        old.reserved = 0;
        if (stack_ptr != 0) {
            if (self->signal_stack_on_alt())
                return -EPERM;
            if (next.flags == ASTIA_SS_DISABLE)
                self->signal_stack(0, 0, ASTIA_SS_DISABLE);
            else
                self->signal_stack(next.sp, next.size, 0);
        }
        if (old_range && !sys::copy_out(old_range.value.bytes(), &old))
            return -EFAULT;
        return 0;
    }

} // namespace astia::proc::syscall
