#pragma once

#include <astia/common.hpp>

// Platform entropy sources for the kernel CSPRNG.  Every hook is optional.
// The CSPRNG classifies them through crypto::entropy_quality: hardware_random
// and wall_clock can satisfy the seed, while cycle_counter is only ever mixed
// as weak material.  A port with neither strong source leaves the pool
// unseeded (EAGAIN) rather than treating jitter as entropy; AArch64 in
// particular must not assume the optional RNDR instruction.

namespace astia::arch {
    /// Fill up to `capacity` words from a hardware random source.
    /// Returns the number of words written; zero when the CPU has none.
    u32 hardware_random(u64* words, u32 capacity);

    /// Monotonic cycle counter with no defined epoch, or zero when the
    /// architecture has no readable counter.  Callers use deltas as weak
    /// mixing material: an observer can model the counter, so it never
    /// decides whether the CSPRNG is seeded.
    u64 cycle_counter();

    /// Raw platform wall clock (one value that changes across boots), or
    /// false when the platform exposes no readable clock yet.
    bool wall_clock(u64& stamp);
} // namespace astia::arch
