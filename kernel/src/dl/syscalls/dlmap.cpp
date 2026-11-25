#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

#include "../dl.hpp"
#include "proc/proc.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::dl::syscalls {

    struct dlmap
        : sys::syscall_handler<dlmap, sys::syscall_id::dlmap, sys::syscall_mode::async> {
        static core::task<i64> invoke(i32 fd, u32 flags,
                                      sys::user_ptr<astia_dl_info> output) {
            auto* process = proc::current_process();
            if (process == nullptr)
                co_return -ESRCH;

            auto range = output.validate();
            if (!range)
                co_return -EFAULT;

            auto mapped = co_await astia::dl::map(*process, fd_t{fd}, flags);
            if (!mapped)
                co_return error_errno(mapped.error);

            astia_dl_info info{};
            info.size    = sizeof(info);
            info.version = ASTIA_DL_INFO_VERSION;
            info.base    = mapped.value.base;
            info.span    = mapped.value.span;
            info.entry   = mapped.value.entry;
            info.phdr    = mapped.value.phdr;
            info.phnum   = mapped.value.phnum;
            info.phent   = mapped.value.phent;
            if (!sys::copy_out(range.value.bytes(), &info))
                co_return -EFAULT;
            co_return static_cast<i64>(mapped.value.base);
        }
    };

} // namespace astia::dl::syscalls
