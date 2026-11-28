#pragma once

#include <astia/core/hook.hpp>

namespace astia::fs::events {
    /// Emitted after openat has resolved and checked its pathname, just
    /// before the backend binds a node.  Fault-injection tests swap the
    /// target here to prove the check and the open observe one identity.
    struct open_checked : core::hook::event<open_checked, const char*> {};
} // namespace astia::fs::events
