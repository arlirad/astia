#pragma once

#include <astia/shutdown.hpp>

namespace astia::proc {
    /// Scheduler-backed shutdown handler executor. Installed by the proc init
    /// step once the scheduler can create kernel threads, so the shutdown
    /// dispatcher can bound every non-final handler.
    extern const shutdown::detail::dispatch_executor shutdown_executor;
} // namespace astia::proc
