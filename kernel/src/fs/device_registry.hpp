#pragma once

namespace astia::fs {
    /// Drop every pending/published registration.  Called by fs::init before
    /// the namespace is published again.
    void reset_device_registry();
} // namespace astia::fs
