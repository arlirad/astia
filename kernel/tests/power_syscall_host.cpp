#include "power_syscall_host.hpp"

#include <astia/acpi.hpp>
#include <astia/api/errno.h>
#include <astia/core/task.hpp>

#include "../src/proc/proc.hpp"
#include "../src/proc/task_run.hpp"

namespace astia::proc {
    namespace {
        alignas(process) unsigned char process_storage[sizeof(process)]{};
    } // namespace

    process* current_process() {
        return host_test::process_present ? reinterpret_cast<process*>(process_storage)
                                          : nullptr;
    }

    i64 process::seteuid(u32 value) {
        arch::interrupt_spinlock_guard guard(_metadata_lock);

        _euid = value;

        return 0;
    }

    i64 run_task(core::task<i64>) {
        return -ENOSYS;
    }
} // namespace astia::proc

namespace astia::acpi {
    bool shutdown_supported() {
        return host_test::shutdown_ready;
    }

    bool shutdown() {
        ++host_test::shutdown_calls;

        return host_test::shutdown_result;
    }

    bool reboot_supported() {
        return host_test::reboot_ready;
    }

    bool reboot() {
        ++host_test::reboot_calls;

        return host_test::reboot_result;
    }
} // namespace astia::acpi
