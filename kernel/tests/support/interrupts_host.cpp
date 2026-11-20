#include <astia/arch/cpu.hpp>

namespace astia::arch {

    static thread_local bool interrupts_enabled = true;

    void cpu::interrupts() {
        interrupts_enabled = true;
    }

    void cpu::nointerrupts() {
        interrupts_enabled = false;
    }

    bool cpu::check_interrupts() {
        return interrupts_enabled;
    }

}
