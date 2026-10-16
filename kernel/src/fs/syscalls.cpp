#include "aio.hpp"
#include "fs.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"
#include <astia/api/aio.h>
#include <astia/api/errno.h>

namespace astia::fs {

    namespace {
        auto sys_io_setup(i64 entries, i64 flags, i64 out, i64, i64, i64) -> i64 {
            // The result slot is written by io_setup; validate before it
            // runs rather than letting the kernel write through a raw
            // pointer.  io_setup never blocks, so there is no window.
            auto* ring_out = reinterpret_cast<astia_rings**>(static_cast<usize>(out));
            if (!sys::user_range_ok(ring_out, sizeof(astia_rings*))) {
                return -EFAULT;
            }
            return sys::io_setup(static_cast<u32>(entries), static_cast<u32>(flags),
                                 ring_out);
        }

        auto sys_io_destroy(i64 ring, i64, i64, i64, i64, i64) -> i64 {
            return sys::io_destroy(
                reinterpret_cast<astia_rings*>(static_cast<usize>(ring)));
        }

        auto sys_io_enter(i64 ring, i64 submit, i64 complete, i64 flags, i64 timeout, i64)
            -> i64 {
            // The ABI's 5th argument is an astia_timespec* (or NULL), not a
            // tick count — io_enter_abi owns that translation.  Copy it in
            // first: the wait below blocks, so nothing user-owned may be
            // held across it.
            const void* user_timeout =
                reinterpret_cast<const void*>(static_cast<usize>(timeout));
            astia_timespec timeout_copy{};
            if (user_timeout != nullptr &&
                !sys::copy_in(&timeout_copy, user_timeout, sizeof(timeout_copy))) {
                return -EFAULT;
            }
            return sys::io_enter_abi(
                reinterpret_cast<astia_rings*>(static_cast<usize>(ring)),
                static_cast<u32>(submit), static_cast<u32>(complete),
                static_cast<u32>(flags),
                user_timeout == nullptr ? nullptr : &timeout_copy);
        }

        auto sys_io_register(i64, i64, i64, i64, i64, i64) -> i64 {
            return -ENOSYS;
        }
    }

    auto register_syscalls() -> bool {
        return sys::register_syscall(sys::syscall_id::io_setup, sys_io_setup) &&
               sys::register_syscall(sys::syscall_id::io_destroy, sys_io_destroy) &&
               sys::register_syscall(sys::syscall_id::io_enter, sys_io_enter) &&
               sys::register_syscall(sys::syscall_id::io_register, sys_io_register);
    }

} // namespace astia::fs
