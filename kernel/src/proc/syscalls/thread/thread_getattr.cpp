#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/memory.hpp>

#include "../../../sys/user_mem.hpp"
#include "../../proc.hpp"
#include "../../process.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct thread_getattr
        : sys::syscall_handler<thread_getattr, sys::syscall_id::thread_getattr,
                               sys::syscall_mode::sync> {
        static i64 invoke(u32 tid, sys::user_ptr<astia_thread_stack_info> output) {
            auto* owner = current_process();
            if (owner == nullptr)
                return -ESRCH;

            const auto validated = output.validate();
            if (!validated)
                return -EFAULT;

            process::thread_stack_snapshot snapshot{};
            auto target = owner->thread_stack_info(tid, snapshot);
            if (!target)
                return -ESRCH;

            if (!snapshot.supported)
                return -ENOTSUP;

            astia_thread_stack_info info;

            fill_zero(&info, sizeof info);

            info.stack_addr = snapshot.stack_base;
            info.stack_size = static_cast<u64>(snapshot.stack_size);
            info.detached   = snapshot.detached ? 1u : 0u;
            info.reserved   = 0u;

            return sys::copy_out(validated.value.bytes(), &info) ? 0 : -EFAULT;
        }
    };
} // namespace astia::proc::syscalls
