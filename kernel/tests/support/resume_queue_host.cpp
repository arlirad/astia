#include <astia/arch/cpu.hpp>

namespace astia::arch {

    static thread_local bool interrupts_enabled = true;

    u32 cpu::current_index() {
        return 0;
    }

    void cpu::interrupts() {
        interrupts_enabled = true;
    }

    void cpu::nointerrupts() {
        interrupts_enabled = false;
    }

    bool cpu::check_interrupts() {
        return interrupts_enabled;
    }

    static thread_local bool reschedule_pending = false;

    void cpu::request_local_reschedule() {
        reschedule_pending = true;
    }

    namespace test {
        void clear_reschedule_request() {
            reschedule_pending = false;
        }

        bool reschedule_requested() {
            return reschedule_pending;
        }
    }

}
