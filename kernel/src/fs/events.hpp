#pragma once

#include <astia/core/hook.hpp>

namespace astia::fs::events {
    /// Emitted after openat has resolved and checked its pathname, just
    /// before the backend binds a node.  Fault-injection tests swap the
    /// target here to prove the check and the open observe one identity.
    struct open_checked : core::hook::event<open_checked, const char*> {};

    /// Emitted after chown has resolved and checked its pathname, just before
    /// the backend applies the ownership change.  Fault-injection tests swap
    /// the target here to prove the check and the change observe one identity.
    struct chown_checked : core::hook::event<chown_checked, const char*> {};
} // namespace astia::fs::events
