#pragma once

namespace astia::proc::host_test {
    inline bool process_present = true;
} // namespace astia::proc::host_test

namespace astia::acpi::host_test {
    inline bool     shutdown_ready  = false;
    inline bool     shutdown_result = false;
    inline unsigned shutdown_calls  = 0;
    inline bool     reboot_ready    = false;
    inline bool     reboot_result   = false;
    inline unsigned reboot_calls    = 0;

    inline void reset() {
        shutdown_ready  = false;
        shutdown_result = false;
        shutdown_calls  = 0;
        reboot_ready    = false;
        reboot_result   = false;
        reboot_calls    = 0;
    }
} // namespace astia::acpi::host_test
