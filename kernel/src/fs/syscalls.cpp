#include "aio.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"
#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/fs.hpp>

namespace astia::fs {

    namespace {
        i64 sys_io_setup(i64 entries, i64 flags, i64 out, i64, i64, i64) {
            // The result slot is written by io_setup; validate before it
            // runs rather than letting the kernel write through a raw
            // pointer.  io_setup never blocks, so there is no window.
            const sys::user_range ring_out =
                sys::user_range::bytes(static_cast<u64>(out), sizeof(astia_rings*));
            if (!sys::user_range_ok(ring_out)) {
                return -EFAULT;
            }
            return sys::io_setup(static_cast<u32>(entries), static_cast<u32>(flags),
                                 reinterpret_cast<astia_rings**>(ring_out.address()));
        }

        i64 sys_io_destroy(i64 ring, i64, i64, i64, i64, i64) {
            return sys::io_destroy(
                reinterpret_cast<astia_rings*>(static_cast<usize>(ring)));
        }

        i64 sys_io_enter(i64 ring, i64 submit, i64 complete, i64 flags, i64 timeout,
                         i64) {
            // The ABI's 5th argument is an astia_timespec* (or NULL), not a
            // tick count — io_enter_abi owns that translation.  Copy it in
            // first: the wait below blocks, so nothing user-owned may be
            // held across it.
            const sys::user_range user_timeout =
                sys::user_range::bytes(static_cast<u64>(timeout), sizeof(astia_timespec));
            astia_timespec timeout_copy{};
            if (!user_timeout.null() && !sys::copy_in(&timeout_copy, user_timeout)) {
                return -EFAULT;
            }
            return sys::io_enter_abi(
                reinterpret_cast<astia_rings*>(static_cast<usize>(ring)),
                static_cast<u32>(submit), static_cast<u32>(complete),
                static_cast<u32>(flags), user_timeout.null() ? nullptr : &timeout_copy);
        }

        i64 sys_io_register(i64, i64, i64, i64, i64, i64) {
            return -ENOSYS;
        }
    }

    bool register_syscalls() {
        return sys::register_syscall(sys::syscall_id::io_setup, sys_io_setup) &&
               sys::register_syscall(sys::syscall_id::io_destroy, sys_io_destroy) &&
               sys::register_syscall(sys::syscall_id::io_enter, sys_io_enter) &&
               sys::register_syscall(sys::syscall_id::io_register, sys_io_register);
    }

} // namespace astia::fs
