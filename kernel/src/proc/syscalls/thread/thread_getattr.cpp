#include "thread_getattr.hpp"

#include "../../proc.hpp"
#include "../../process.hpp"
#include "../../../sys/user_mem.hpp"
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

namespace astia::proc::syscall {

    i64 thread_getattr(u32 tid, u64 output) {
        auto* owner = current_process();
        if (owner == nullptr)
            return -ESRCH;

        const auto validated =
            sys::user_ptr<astia_thread_stack_info>(output).validate();
        if (!validated)
            return -EFAULT;

        process::thread_stack_snapshot snapshot{};
        auto target = owner->thread_stack_info(tid, snapshot);
        if (!target)
            return -ESRCH;
        if (!snapshot.supported)
            return -ENOTSUP;

        astia_thread_stack_info info{
            snapshot.stack_base, static_cast<u64>(snapshot.stack_size),
            snapshot.detached ? 1u : 0u, 0u};
        return sys::copy_out(validated.value.bytes(), &info) ? 0 : -EFAULT;
    }

}
