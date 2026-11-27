#pragma once

#include <astia/common.hpp>

namespace astia::crypto {
    /// True once the kernel generator holds a key.  Seeding happens on the
    /// first request, from the platform entropy sources.
    bool seeded();

    /// Fill `length` bytes from the kernel generator.  Returns false when the
    /// platform has no usable entropy source: callers must fail (EAGAIN)
    /// rather than substitute a predictable value.
    bool random_bytes(void* buffer, usz length);

    /// Rekey the generator for a forked child.  The kernel generator is
    /// global, so parent and child already consume one stream; the rekey
    /// additionally makes any future per-process state diverge.
    void fork_child(u64 child_pid);
} // namespace astia::crypto
