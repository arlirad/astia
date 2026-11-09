#pragma once

#include <astia/common.hpp>

namespace astia::arch {

    /// Attributes for a mapping. The values are abstract (arch-neutral);
    /// each arch translates them into its hardware page-table entry bits.
    enum class page_flags : u64 {
        none          = 0,
        present       = 1ull << 0,
        writable      = 1ull << 1,
        user          = 1ull << 2,
        write_through = 1ull << 3,
        cache_disable = 1ull << 4,
        global        = 1ull << 5,
        no_execute    = 1ull << 6,
    };

    constexpr page_flags operator|(page_flags a, page_flags b) {
        return static_cast<page_flags>(static_cast<u64>(a) | static_cast<u64>(b));
    }

    constexpr bool has_flag(page_flags value, page_flags bit) {
        return (static_cast<u64>(value) & static_cast<u64>(bit)) != 0;
    }

    /// One address space: one root table, one arch "load this" value.
    ///
    /// The *declaration* is the arch abstraction: layout, flags and semantics
    /// are neutral here, and every method is implemented by the selected
    /// architecture in src/arch/<arch>/page_map.cpp (only one arch is linked
    /// into a given build).
    ///
    /// A map may carry a `parent`, normally the kernel's own map:
    ///   - create(parent) copies the parent's kernel-half top-level entries
    ///     into the new root, so the hardware walk of a child (process)
    ///     address space sees the kernel mappings — including page tables
    ///     the kernel allocates later, because the entries are shared, not
    ///     duplicated;
    ///   - translate() falls back to the parent when this map has no entry
    ///     at all for an address (e.g. the kernel filled in a slot after
    ///     the child was created).
    /// map_4k/map_2m/unmap only ever touch *this* map's tables — kernel
    /// mappings are made on the kernel map, not on a process map.
    ///
    /// The one top-level entry that is never shared is the arch's recursive
    /// slot (x86_64: 510, arch::recursive_base): create() points it at the
    /// new map's *own* root, giving every map a recursive window onto its
    /// own page tables — so the active map's tables can be reached by
    /// arithmetic on virtual addresses, without any 1:1 identity mapping.
    ///
    /// Intermediate tables come from the kernel frame allocator on demand and
    /// are written through the identity window, so the active map must keep
    /// low physical memory identity-mapped (the kernel map does; see the arch
    /// paging init step).
    class page_map {
        public:
        static constexpr paddr_t invalid = ~paddr_t{0};

        /// Empty (invalid) map; a placeholder for create()/assignment.
        page_map() = default;
        ~page_map();

        /// Allocate a fresh, zeroed root table. Check valid() before use.
        /// With `parent`, the child also starts out seeing the parent's
        /// kernel half.
        static page_map create(const page_map* parent = nullptr);

        page_map(const page_map&)            = delete;
        page_map& operator=(const page_map&) = delete;
        page_map(page_map&& other) noexcept;
        page_map& operator=(page_map&& other) noexcept;

        bool valid() const {
            return root_ != 0;
        }

        /// Physical address of the root table (what the arch loads).
        paddr_t root() const {
            return root_;
        }

        /// The parent this map was created with, or nullptr.
        const page_map* parent() const {
            return parent_;
        }

        /// Map one 4 KiB page. Splits a 2 MiB page if one is in the way.
        bool map_4k(vaddr_t virt, paddr_t phys, page_flags flags);

        /// Map one 2 MiB page. Fails if a 4 KiB table already covers `virt`.
        bool map_2m(vaddr_t virt, paddr_t phys, page_flags flags);

        /// Remove a 4 KiB mapping of *this* map. 2 MiB mappings are not
        /// touched (false); the parent's mappings are never touched.
        bool unmap(vaddr_t virt);

        bool protect_4k(vaddr_t virt, page_flags flags);

        /// Virtual to physical, or invalid. Consults the parent when this
        /// map has no entry of its own.
        paddr_t translate(vaddr_t virt) const;
        bool    writable(vaddr_t virt) const;
        bool    user_accessible(vaddr_t virt) const;
        bool    executable(vaddr_t virt) const;

        /// Install this page map (arch equivalent: load the root register).
        void activate() const;

        /// Invalidate any TLB entry for `virt`.
        static void invalidate(vaddr_t virt);

        private:
        explicit page_map(paddr_t root, const page_map* parent)
            : root_(root), parent_(parent) {}

        paddr_t         root_   = 0;
        const page_map* parent_ = nullptr;
    };

    /// The kernel's own address space, installed by the arch paging init
    /// step; nullptr until then. Provided per arch.
    page_map* kernel_page_map();

} // namespace astia::arch
