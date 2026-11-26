#include "ps2_irq_host.hpp"

#include <astia/arch/interrupts.hpp>

#include "../../src/dev/ps2_irq.hpp"

namespace astia::dev::ps2 {
    namespace {
        bool irq_masked = false;
    }

    bool claim_guarded(arch::irq_handler handler, arch::irq_callback_enter enter,
                       arch::irq_callback_leave leave, void* context) {
        return astia::arch::register_irq_handler(1, handler, enter, leave, context);
    }

    bool mask() {
        irq_masked = true;

        return astia::arch::mask_irq_handler(1);
    }

    bool unregister() {
        return astia::arch::unregister_irq_handler(1);
    }

    bool unmask() {
        irq_masked = false;

        return true;
    }

    namespace host_test {
        bool masked() {
            return irq_masked;
        }
    } // namespace host_test
} // namespace astia::dev::ps2
