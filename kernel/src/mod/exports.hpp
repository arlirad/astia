#pragma once

#include <astia/common.hpp>

// The kernel's module export table: the set of kernel symbols a loadable
// module may bind against. This is deliberately *not* the kernel's whole symbol
// surface — a module can only see what has been explicitly exposed, so the
// kernel's internal symbols stay free to change.
//
// Pure bookkeeping (no mm / frame-allocator dependency), so it is host-testable
// on its own.

namespace astia::mod {

    /// Upper bound on exported symbols. Fixed capacity keeps the table off the
    /// heap, mirroring memory_map / init's fixed-capacity style.
    inline constexpr usize max_exports = 64;

    /// Make a kernel symbol available to modules under `name`.
    ///
    /// `name` must be a stable string (typically a string literal); it is
    /// stored by pointer, not copied — there is no heap here. Returns false if
    /// the table is full or `name` is already present.
    ///
    /// `addr` is a u64 (not a pointer) so callers can publish function
    /// addresses without casting a function pointer through an object pointer,
    /// which `-pedantic` rejects.
    bool expose(const char* name, u64 addr);

    /// Resolve a symbol previously exposed with expose(). Returns 0 if unknown.
    /// This backs the ELF loader's undefined-symbol resolution.
    u64 resolve(const char* name);

} // namespace astia::mod
