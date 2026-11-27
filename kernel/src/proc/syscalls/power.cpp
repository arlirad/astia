#include <astia/acpi.hpp>
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

#include "proc.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct power
        : sys::syscall_handler<power, sys::syscall_id::power, sys::syscall_mode::sync> {
        static i64 invoke(u32 operation) {
            switch (operation) {
            case ASTIA_POWER_SHUTDOWN:
            case ASTIA_POWER_REBOOT:
            case ASTIA_POWER_SUSPEND:
                break;
            default:
                return -EINVAL;
            }

            auto* process = current_process();
            if (process == nullptr)
                return -ESRCH;

            if (process->euid() != 0)
                return -EPERM;

            switch (operation) {
            case ASTIA_POWER_SHUTDOWN:
                if (!acpi::shutdown_supported())
                    return -ENOTSUP;

                return acpi::shutdown() ? 0 : -EIO;
            case ASTIA_POWER_REBOOT:
                if (!acpi::reboot_supported())
                    return -ENOTSUP;

                return acpi::reboot() ? 0 : -EIO;
            case ASTIA_POWER_SUSPEND:
                // The ACPI layer has no S3 entry or wake path.  This must not
                // fall back to a process/thread sleep.
                return -ENOTSUP;
            default:
                return -EINVAL;
            }
        }
    };
} // namespace astia::proc::syscalls

#if defined(ASTIA_MODULE_HOST_TEST)
namespace {
    struct power_host_registration {
        power_host_registration() {
            (void) astia::sys::register_descriptor(
                &astia::sys::syscall_handler<astia::proc::syscalls::power,
                                             astia::sys::syscall_id::power,
                                             astia::sys::syscall_mode::sync>::descriptor);
        }
    } power_host_registration_instance;
} // namespace
#endif
