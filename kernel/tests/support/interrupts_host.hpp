#pragma once

#include <astia/common.hpp>

namespace astia::arch::host_test {
    /// Deliver a synthetic PIC line through the host binding table.
    void dispatch_irq(u8 irq);
} // namespace astia::arch::host_test
