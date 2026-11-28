#include "power_common.h"

int main(int argc, char** argv) {
    return cu_power_main(argc, argv, "poweroff",
                         "Power off the system through the ACPI shutdown path.",
                         ASTIA_POWER_SHUTDOWN);
}
