#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

#include "../module.hpp"
#include "../syscall_abi.hpp"
#include "proc/proc.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::mod::syscalls {
    struct modstat : sys::syscall_handler<modstat, sys::syscall_id::modstat,
                                          sys::syscall_mode::sync> {
        static i64 invoke(sys::user_ptr<const astia_module_id> input,
                          sys::user_ptr<astia_module_stat>     output) {
            auto* process = proc::current_process();
            if (process == nullptr)
                return -ESRCH;

            if (process->euid() != 0)
                return -EPERM;

            auto out = output.validate();
            if (!out)
                return -EFAULT;

            auto handle = input.validate();
            if (!handle)
                return -EFAULT;

            astia_module_id public_id{};

            if (!sys::copy_in(&public_id, handle.value.bytes()))
                return -EFAULT;

            module_id          id{};
            const module_error decoded = decode_public_id(public_id, id);

            if (decoded != module_error::ok)
                return module_errno(decoded);

            module_info info{};
            module_id   dependencies[ASTIA_MODULE_MAX_DEPENDENCIES]{};

            usize dependency_total = 0;

            const module_error snap = snapshot_module(
                id, info, span<module_id>(dependencies, ASTIA_MODULE_MAX_DEPENDENCIES),
                dependency_total);

            if (snap != module_error::ok)
                return module_errno(snap);

            astia_module_stat stat{};

            fill_module_stat(info, dependencies, dependency_total, stat);

            u8 stat_bytes[sizeof stat]{};

            __builtin_memcpy(stat_bytes, &stat, sizeof stat);

            if (!sys::copy_out(out.value.bytes(), stat_bytes))
                return -EFAULT;

            return 0;
        }
    };
} // namespace astia::mod::syscalls

#if defined(ASTIA_MODULE_HOST_TEST)
namespace {
    struct modstat_host_registration {
        modstat_host_registration() {
            (void) astia::sys::register_descriptor(
                &astia::sys::syscall_handler<astia::mod::syscalls::modstat,
                                             astia::sys::syscall_id::modstat,
                                             astia::sys::syscall_mode::sync>::descriptor);
        }
    } modstat_host_registration_instance;
} // namespace
#endif
