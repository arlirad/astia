#pragma once

#include <astia/common.hpp>

namespace astia::shutdown {
    /// Ordered shutdown phases. Every registered handler in a lower tier runs
    /// before any handler in a higher tier. The platform tier is the last
    /// subsystem quiesce point; the final tier is the single irreversible
    /// platform transition selected for the dispatch (registered ACPI S5 by
    /// default, or a one-shot override such as validated reset) and always runs
    /// last.
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
    /// best-effort, and one failed handler must not block the final platform
    /// transition. The dispatch as a whole reports false if any handler failed,
    /// timed out, or if no platform/final handler was registered. Only one
    /// final-tier handler may be registered, so the irreversible transition
    /// cannot be followed by more firmware-driven work.
    ///
    /// When the scheduler provides a dispatch executor, each non-final handler
    /// runs on its own kernel thread and is bounded by handler_timeout_ns and
    /// handler_max_polls. A handler that exceeds either bound is abandoned: it
    /// is logged as failed, the remaining tiers still run, and the final
    /// transition remains reachable. A timed-out handler may still be running
    /// and may still hold resources; the watchdog makes the dispatch
    /// deterministic, it does not make an arbitrary handler safe to cancel.
    /// The final tier (registered or override) always runs on the caller,
    /// unbounded, because the irreversible transition may not return.
    using handler = bool (*)(void* context);

    /// Upper bound one non-final handler may consume before the watchdog
    /// abandons it. Chosen long enough for a flush of real storage and short
    /// enough that one stuck driver cannot deny the power transition.
    inline constexpr u64 handler_timeout_ns = 5ull * nanoseconds_per_second;

    /// Idle-poll backstop for the handler watchdog. The clock can be missing
    /// (before the first tick) or stuck, so the poll budget alone must also
    /// bound a handler.
    inline constexpr u32 handler_max_polls = 1u << 20;

    /// Register one handler. Registration order within a tier is preserved and
    /// is the dispatch order. Registration must happen before initiate();
    /// registrations after dispatch starts are rejected. The name is a static
    /// string used only for diagnostics. At most one `tier::final` handler may
    /// be registered.
    bool register_handler(tier level, const char* name, handler fn,
                          void* context = nullptr);

    /// Run every registered handler once, in tier order. The first caller wins;
    /// later calls return false without running anything. A handler that times
    /// out is treated as a failure and the dispatch continues with the next
    /// handler and tier.
    ///
    /// `final_override` is a one-shot action for this dispatch only. When set,
    /// the registered final handler is skipped and the override runs last, after
    /// the platform tier. It does not register or replace the final handler and
    /// the single-final-registration invariant is unchanged. `shutdown()` uses
    /// the registered final action; reboot passes its validated reset action.
    bool initiate(handler final_override = nullptr, void* final_context = nullptr);

    namespace detail {
        inline constexpr usz handler_capacity = 32;

        /// Internal seam between the dispatcher and whatever can give a handler
        /// its own execution context. The kernel installs a scheduler-backed
        /// executor once the scheduler is online; until then, and in host tests
        /// without one, handlers run on the caller unbounded (the pre-watchdog
        /// behaviour).
        ///
        /// `start` must make `fn(context)` observable through `poll` and return
        /// false when it cannot. `poll` returns 0 while the handler is running,
        /// 1 when it finished successfully, and 2 when it finished with failure.
        /// `abandon` marks a still-running handler as timed out; the handler may
        /// keep running. `relax` lets the handler make progress. `now_ns`
        /// returns monotonic nanoseconds, or 0 when no clock is available.
        struct dispatch_executor {
            bool (*start)(handler fn, void* context, void** task) = nullptr;
            int (*poll)(void* task)                               = nullptr;
            void (*abandon)(void* task)                           = nullptr;
            void (*relax)()                                       = nullptr;
            u64 (*now_ns)()                                       = nullptr;
        };

        /// Install the executor for subsequent dispatches. Rejects null tables
        /// and installation after a dispatch has started. Passing nullptr
        /// restores the synchronous fallback.
        bool set_dispatch_executor(const dispatch_executor* executor);
    } // namespace detail

#ifdef ASTIA_HOST_TEST
    namespace detail {
        void reset_for_test();
    } // namespace detail
#endif
} // namespace astia::shutdown
