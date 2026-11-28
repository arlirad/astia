#include "power_common.h"

int main(int argc, char** argv) {
    return cu_power_main(argc, argv, "reboot",
                         "Restart the system through the firmware reset path.",
                         ASTIA_POWER_REBOOT);
}
