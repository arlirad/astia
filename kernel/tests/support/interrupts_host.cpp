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

    u32 cpu::current_index() {
        return 0;
    }

    void cpu::request_local_reschedule() {}
}
