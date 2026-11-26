#pragma once

#include <astia/common.hpp>

// Platform entropy sources for the kernel CSPRNG.  Every hook is optional:
// an architecture that has no hardware RNG, wall clock, or cycle counter
// reports zero/false and the generic jitter path still contributes what it
// can.  AArch64 in particular must not assume the optional RNDR instruction.

namespace astia::arch {
    /// Fill up to `capacity` words from a hardware random source.
    /// Returns the number of words written; zero when the CPU has none.
    u32 hardware_random(u64* words, u32 capacity);

    /// Monotonic cycle counter with no defined epoch, or zero when the
    /// architecture has no readable counter.  Callers use deltas as jitter.
    u64 cycle_counter();

    /// Raw platform wall clock (one value that changes across boots), or
    /// false when the platform exposes no readable clock yet.
    bool wall_clock(u64& stamp);
} // namespace astia::arch
