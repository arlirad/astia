#pragma once

#include "exports.hpp"
#include "types.hpp"
#include <astia/common.hpp>
#include <astia/span.hpp>

// Kernel module registry.
//
// A module is a relocatable x86-64 ELF object that exports a `module_main`
// entry point. The kernel loads it into frame-allocator memory, resolves its
// undefined symbols against the export table (see "exports.hpp"),
// applies relocations, and calls `module_main`.
//
// This layer owns the policy (where memory comes from, what a symbol means);
// the ELF mechanics live in <astia/elf.hpp>.

namespace astia::mod {

    /// A loaded kernel module.
    struct module {
        const char* name; ///< Module name (for diagnostics).
        void*       base; ///< Load address of the image allocation.
        usize       size; ///< Bytes allocated for the image.
        int (*entry)();   ///< Resolved module_main().
        bool running;     ///< True once entry() has been called.
    };

    /// Upper bound on simultaneously loaded modules. Fixed capacity keeps the
    /// registry off the heap.
    inline constexpr usize max_modules = 16;

    /// Load a module from a relocatable ELF image at `image`/`size`, resolve
    /// and relocate it, and register it.
    ///
    /// On success returns the registered module (whose entry point has NOT yet
    /// been called — call run_module() for that). Returns null on any failure.
    ///
    /// `image` need not stay valid after the call: section contents are copied
    /// into frame-allocator memory.
    const module*        load_module(span<const u8> image);
    inline const module* load_module(const void* image, usize size) {
        return load_module(span<const u8>(static_cast<const u8*>(image), size));
    }

    /// Unload a module previously returned by load_module(). Frees the image
    /// allocation and drops the registry slot. Returns false if `mod` is not a
    /// registered module.
    bool unload_module(const module* mod);

    /// Call a loaded module's entry point. Returns the module's return code, or
    /// -1 if `mod` is null or already running.
    int run_module(const module* mod);

    /// Number of currently loaded modules.
    usize count();

    /// The i-th loaded module, or null if `i` is out of range.
    const module* at(usize i);

} // namespace astia::mod
