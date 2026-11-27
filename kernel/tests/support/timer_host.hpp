#pragma once

#include <astia/common.hpp>

namespace astia::test {
    /// Deterministic timer service for host tests.  The shim backs
    /// `arch::timer_ticks`/`timer_hz` and the kernel timer queue, so tests can
    /// advance time and fire callbacks without a real interrupt source.
    void timer_reset();

    void timer_advance(u64 ticks);
} // namespace astia::test
