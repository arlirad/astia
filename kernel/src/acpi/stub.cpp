#include <astia/acpi.hpp>

namespace astia::acpi {
    u64 rsdp() {
        return 0;
    }

    bool shutdown_supported() {
        return false;
    }

    bool shutdown() {
        return false;
    }
} // namespace astia::acpi
