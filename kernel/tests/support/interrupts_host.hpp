#pragma once

#include <astia/common.hpp>

namespace astia::arch::host_test {
    /// Deliver a synthetic PIC line through the host binding table.
    void dispatch_irq(u8 irq);

    /// Observe the unconditional EOI that follows a dispatch (after the
    /// handler and guarded leave). Pass nullptr to clear.
    void set_eoi_observer(void (*observer)(u8 irq));

    /// Whether a handler (guarded or plain) is currently bound to `irq`.
    bool irq_registered(u8 irq);
} // namespace astia::arch::host_test
