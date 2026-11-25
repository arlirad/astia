#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

#include "../module.hpp"
#include "../syscall_abi.hpp"
#include "proc/proc.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::mod::syscalls {

    struct modclose
        : sys::syscall_handler<modclose, sys::syscall_id::modclose,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::user_ptr<const astia_module_id> input) {
            auto* process = proc::current_process();
            if (process == nullptr) {
                co_return -ESRCH;
            }
            if (process->euid() != 0) {
                co_return -EPERM;
            }
            auto handle = input.validate();
            if (!handle) {
                co_return -EFAULT;
            }

            astia_module_id public_id{};
            if (!sys::copy_in(&public_id, handle.value.bytes())) {
                co_return -EFAULT;
            }
            module_id id{};
            const module_error decoded = decode_public_id(public_id, id);
            if (decoded != module_error::ok) {
                co_return module_errno(decoded);
            }

            const auto closed = co_await close(id);
            co_return module_close_errno(closed);
        }
    };

} // namespace astia::mod::syscalls
