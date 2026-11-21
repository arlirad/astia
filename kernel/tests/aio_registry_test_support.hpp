#pragma once

namespace astia::io::aio::test_support {

    enum class owner : unsigned {
        core,
        filesystem,
        readiness,
        socket,
        unknown,
    };

    extern unsigned core_dispatches;
    extern unsigned filesystem_dispatches;
    extern unsigned readiness_dispatches;
    extern unsigned socket_dispatches;
    extern unsigned unknown_completions;

    void reset_registry_counters();
    bool routes_to(owner expected, unsigned operation);

} // namespace astia::io::aio::test_support
