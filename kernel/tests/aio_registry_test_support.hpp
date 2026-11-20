#pragma once

namespace astia::io::aio::test_support {

    extern unsigned core_dispatches;
    extern unsigned filesystem_dispatches;
    extern unsigned readiness_dispatches;
    extern unsigned socket_dispatches;
    extern unsigned unknown_completions;

    void reset_registry_counters();

} // namespace astia::io::aio::test_support
