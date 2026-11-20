#pragma once

#include <astia/common.hpp>

#include <syscall.hpp>

namespace astia::fs::syscall {
    /// mount - install a registered native filesystem at an existing directory.
    ///
    /// All names are copied before authorization.  The operation is root-only;
    /// it changes only the in-memory VFS namespace and never opens a host path.
    long mount(u64 type, u64 target, u64 source);
}
