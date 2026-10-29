#pragma once

#include <astia/common.hpp>

namespace astia::fs::syscall {
    /// unmount - root-only teardown of a native VFS mount.
    ///
    /// The target is copied from userspace before authorization and lookup.
    /// The root mount, a mount with open descriptions, and a mount containing
    /// another mount return -EBUSY.  This only changes the in-memory namespace.
    long unmount(u64 target);
}
