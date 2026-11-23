#include "psci.hpp"

#include "types.hpp"

namespace astia::smoke::psci {
    namespace {
        constexpr u64 system_off_function = 0x84000008ULL;
    } // namespace

    [[noreturn]] void system_off() {
        __asm__ volatile("mov x0, %0\n\thvc #0"
                         :
                         : "r"(system_off_function)
                         : "x0", "memory");

        for (;;) {
            __asm__ volatile("wfe");
        }
    }
} // namespace astia::smoke::psci
