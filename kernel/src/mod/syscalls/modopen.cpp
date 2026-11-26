#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

#include "../module.hpp"
#include "../syscall_abi.hpp"
#include "proc/proc.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::mod::syscalls {
    struct modopen : sys::syscall_handler<modopen, sys::syscall_id::modopen,
                                          sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::user_ptr<const char> path, u32 flags,
                                      sys::user_ptr<astia_module_id> output) {
            auto* process = proc::current_process();
            if (process == nullptr)
                co_return -ESRCH;

            if (process->euid() != 0)
                co_return -EPERM;

            if (flags != ASTIA_MODULE_OPEN_NONE)
                co_return -EINVAL;

            auto out = output.validate();
            if (!out)
                co_return -EFAULT;

            char kernel_path[ASTIA_MODULE_PATH_MAX]{};

            if (!sys::copy_in_string(span<char>(kernel_path, sizeof kernel_path),
                                     path.string()))
                co_return -EFAULT;

            module_load_options options{};

            module_name_from_path(kernel_path, options.name);

            const auto opened = co_await open_path(*process, kernel_path, options);
            if (!opened)
                co_return opened.error == error_t::enomem ? -ENOMEM : -EIO;

            if (opened.value.error != module_error::ok)
                co_return module_errno(opened.value.error);

            astia_module_id id{};

            if (!encode_public_id(opened.value.id, id))
                co_return -EINVAL;

            // The coroutine resumes on a kernel worker after I/O, where
            // current_process() is not necessarily this caller.  Copy out
            // through the process captured at entry.
            if (!sys::copy_out_for(process, out.value.bytes(), &id)) {
                const auto rollback = co_await close(opened.value.id);

                if (!rollback || rollback.value != module_error::ok)
                    co_return -EIO;

                co_return -EFAULT;
            }

            co_return 0;
        }
    };
} // namespace astia::mod::syscalls

#if defined(ASTIA_MODULE_HOST_TEST)
namespace {
    struct modopen_host_registration {
        modopen_host_registration() {
            (void) astia::sys::register_descriptor(
                &astia::sys::syscall_handler<
                    astia::mod::syscalls::modopen, astia::sys::syscall_id::modopen,
                    astia::sys::syscall_mode::async>::descriptor);
        }
    } modopen_host_registration_instance;
} // namespace
#endif
