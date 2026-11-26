#pragma once

#include <astia/arch/interrupts.hpp>

// Kernel-private PS/2 line hooks. The raw PIO bus and the unguarded irq_line
// stay inside arch/x86_64/platform.cpp; only the module registry may bind a
// guarded route through this header.

namespace astia::dev::ps2 {
    bool claim_guarded(arch::irq_handler handler, arch::irq_callback_enter enter,
                       arch::irq_callback_leave leave, void* context);
    bool mask();
    bool unregister();
    bool unmask();
} // namespace astia::dev::ps2
