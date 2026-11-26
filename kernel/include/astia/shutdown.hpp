#pragma once

#include <astia/common.hpp>

namespace astia::shutdown {
    /// Ordered shutdown phases. Every registered handler in a lower tier runs
    /// before any handler in a higher tier. The platform tier is the last
    /// subsystem quiesce point; the final tier is reserved for the single
    /// irreversible poweroff action (ACPI S5) and always runs last.
    enum class tier : u8 {
        prepare,
        flush,
        quiesce,
        platform,
        final,
        count,
    };

    /// A handler returns true on success. A false result is logged and the
    /// remaining handlers, including the final tier, still run: shutdown is
    /// best-effort, and one failed flush must not block the power-off attempt.
    /// The dispatch as a whole reports false if any handler failed or if no
    /// platform/final handler was registered. Only one final-tier handler may
    /// be registered, so the irreversible poweroff cannot be followed by more
    /// firmware-driven work.
    using handler = bool (*)(void* context);

    /// Register one handler. Registration order within a tier is preserved and
    /// is the dispatch order. Registration must happen before initiate();
    /// registrations after dispatch starts are rejected. The name is a static
    /// string used only for diagnostics. At most one `tier::final` handler may
    /// be registered.
    bool register_handler(tier level, const char* name, handler fn,
                          void* context = nullptr);

    /// Run every registered handler once, in tier order. The first caller wins;
    /// later calls return false without running anything.
    bool initiate();

#ifdef ASTIA_HOST_TEST
    namespace detail {
        void reset_for_test();
    } // namespace detail
#endif
} // namespace astia::shutdown
