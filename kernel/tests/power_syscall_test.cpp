#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

#include "../src/proc/proc.hpp"
#include "../src/sys/syscall.hpp"
#include "power_syscall_host.hpp"
#include "support/test.hpp"

namespace {
    void reset_host() {
        astia::proc::host_test::process_present = true;
        astia::acpi::host_test::reset();
        (void) astia::proc::current_process()->seteuid(0);
    }

    void set_root(bool root) {
        (void) astia::proc::current_process()->seteuid(root ? 0u : 1u);
    }

    astia::i64 call_power(astia::u32 operation) {
        astia::arch::syscall_return image{};

        return astia::sys::dispatch(astia::sys::syscall_id::power, operation, 0, 0, 0, 0,
                                    0, image);
    }
} // namespace

ASTIA_TEST(power_syscall_ids_and_operation_values_are_canonical) {
    static_assert(static_cast<astia::u32>(astia::sys::syscall_id::power) == 79);
    static_assert(SYS_POWER == 79);
    static_assert(ASTIA_POWER_SHUTDOWN == 0);
    static_assert(ASTIA_POWER_REBOOT == 1);
    static_assert(ASTIA_POWER_SUSPEND == 2);
}

ASTIA_TEST(power_syscall_rejects_unknown_operation_before_authorization) {
    reset_host();
    set_root(false);
    astia::proc::host_test::process_present = false;

    ASTIA_CHECK(call_power(3) == -EINVAL);
    ASTIA_CHECK(call_power(0xffffffffu) == -EINVAL);
}

ASTIA_TEST(power_syscall_requires_a_current_process) {
    reset_host();
    astia::acpi::host_test::shutdown_ready  = true;
    astia::proc::host_test::process_present = false;

    ASTIA_CHECK(call_power(ASTIA_POWER_SHUTDOWN) == -ESRCH);
    ASTIA_CHECK(call_power(ASTIA_POWER_REBOOT) == -ESRCH);
    ASTIA_CHECK(call_power(ASTIA_POWER_SUSPEND) == -ESRCH);
    ASTIA_CHECK(astia::acpi::host_test::shutdown_calls == 0);
    ASTIA_CHECK(astia::acpi::host_test::reboot_calls == 0);
}

ASTIA_TEST(power_syscall_requires_root) {
    reset_host();
    set_root(false);
    astia::acpi::host_test::shutdown_ready = true;
    astia::acpi::host_test::reboot_ready   = true;

    ASTIA_CHECK(call_power(ASTIA_POWER_SHUTDOWN) == -EPERM);
    ASTIA_CHECK(call_power(ASTIA_POWER_REBOOT) == -EPERM);
    ASTIA_CHECK(call_power(ASTIA_POWER_SUSPEND) == -EPERM);
    ASTIA_CHECK(astia::acpi::host_test::shutdown_calls == 0);
    ASTIA_CHECK(astia::acpi::host_test::reboot_calls == 0);
}

ASTIA_TEST(power_syscall_shutdown_maps_support_and_dispatch_results) {
    reset_host();
    set_root(true);

    ASTIA_CHECK(call_power(ASTIA_POWER_SHUTDOWN) == -ENOTSUP);
    ASTIA_CHECK(astia::acpi::host_test::shutdown_calls == 0);

    astia::acpi::host_test::shutdown_ready  = true;
    astia::acpi::host_test::shutdown_result = false;

    ASTIA_CHECK(call_power(ASTIA_POWER_SHUTDOWN) == -EIO);
    ASTIA_CHECK(astia::acpi::host_test::shutdown_calls == 1);

    astia::acpi::host_test::shutdown_result = true;

    ASTIA_CHECK(call_power(ASTIA_POWER_SHUTDOWN) == 0);
    ASTIA_CHECK(astia::acpi::host_test::shutdown_calls == 2);
}

ASTIA_TEST(power_syscall_reboot_maps_support_and_dispatch_results) {
    reset_host();
    set_root(true);

    ASTIA_CHECK(call_power(ASTIA_POWER_REBOOT) == -ENOTSUP);
    ASTIA_CHECK(astia::acpi::host_test::reboot_calls == 0);

    astia::acpi::host_test::reboot_ready  = true;
    astia::acpi::host_test::reboot_result = false;

    ASTIA_CHECK(call_power(ASTIA_POWER_REBOOT) == -EIO);
    ASTIA_CHECK(astia::acpi::host_test::reboot_calls == 1);

    astia::acpi::host_test::reboot_result = true;

    ASTIA_CHECK(call_power(ASTIA_POWER_REBOOT) == 0);
    ASTIA_CHECK(astia::acpi::host_test::reboot_calls == 2);
}

ASTIA_TEST(power_syscall_suspend_is_explicitly_unsupported) {
    reset_host();
    set_root(true);
    astia::acpi::host_test::shutdown_ready = true;
    astia::acpi::host_test::reboot_ready   = true;

    ASTIA_CHECK(call_power(ASTIA_POWER_SUSPEND) == -ENOTSUP);
    ASTIA_CHECK(astia::acpi::host_test::shutdown_calls == 0);
    ASTIA_CHECK(astia::acpi::host_test::reboot_calls == 0);
}
