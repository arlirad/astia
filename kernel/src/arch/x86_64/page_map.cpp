#include <astia/arch/layout.hpp>
#include <astia/arch/page_map.hpp>
#include <astia/mm/frame_allocator.hpp>

// x86_64 4-level paging (Intel SDM Vol. 3A, "IA-32e Paging"):
//   virtual address = PML4[47:39] PDPT[38:30] PD[29:21] PT[20:12] offset[11:0]
// Each table is 4 KiB holding 512 eight-byte entries; an entry's physical
// base lives in bits 51:12 (or 51:21 / 51:30 for 2 MiB / 1 GiB pages).
//
// This file is the x86_64 implementation of the shared
// astia::arch::page_map declaration (<astia/arch/page_map.hpp>): the layout
// and semantics are arch-neutral, the entry encoding is not.
//
// Table access does not lean on a 1:1 (identity) mapping:
//   - the *active* map reaches its own tables through the recursive slot:
//     every root reserves PML4[510] pointing back at itself, so each table
//     on a walk to `virt` sits at a virtual address derived from `virt`
//     alone (see alias());
//   - any other map resolves table frames through the kernel direct map
//     (kernel_base + phys), which covers every frame no matter where in
//     physical memory it sits;
//   - only before the first activate() — early construction and host unit
//     tests, where `phys` is a real pointer — does access fall back to the
//     identity window.
namespace astia::arch {

    namespace {

        constexpr u64 entry_present       = 1ull << 0;
        constexpr u64 entry_writable      = 1ull << 1;
        constexpr u64 entry_user          = 1ull << 2;
        constexpr u64 entry_write_through = 1ull << 3;
        constexpr u64 entry_cache_disable = 1ull << 4;
        constexpr u64 entry_huge          = 1ull << 7;
        constexpr u64 entry_global        = 1ull << 8;
        constexpr u64 entry_no_execute    = 1ull << 63;

        // Physical base of a table/4 KiB page lives in bits 51:12.
        constexpr u64 address_mask = 0x000FFFFFFFFFF000ull;

        // Top-level slots [256, 512) form the canonical higher half — slot
        // 256 is exactly kernel_base (0xFFFF800000000000). That is the
        // "kernel half" create() shares from a parent into a child.
        constexpr usize kernel_slot = 256;

        // The one top-level slot each map keeps for itself: its entry points
        // back at the map's own root (recursive mapping; the window it opens
        // is arch::recursive_base). create() writes it after any parent copy,
        // and map_4k/map_2m/unmap refuse to operate inside the window.
        constexpr usize recursive_slot = 510;

        // The map currently installed in the hardware root register — set by
        // activate(). Null before the first activation and always null in
        // host unit tests, which is exactly when table access has to use
        // physical pointers instead of the recursive window.
        const page_map* active_map = nullptr;

        enum class table_level {
            root,
            pdpt,
            pd,
            pt,
        };

        constexpr usize pml4_index(vaddr_t virt) {
            return (virt >> 39) & 0x1ff;
        }
        constexpr usize pdpt_index(vaddr_t virt) {
            return (virt >> 30) & 0x1ff;
        }
        constexpr usize pd_index(vaddr_t virt) {
            return (virt >> 21) & 0x1ff;
        }
        constexpr usize pt_index(vaddr_t virt) {
            return (virt >> 12) & 0x1ff;
        }

        bool is_canonical(vaddr_t virt) {
            const u64 top = virt >> 47;
            return top == 0 || top == 0x1ffff;
        }

        /// Translate the arch-neutral flags into an x86_64 leaf entry. A
        /// mapping is always present; page_flags::present is a call-site
        /// convention, not a way to make an absent mapping.
        u64 to_leaf(page_flags flags) {
            u64 bits = entry_present;
            if (has_flag(flags, page_flags::writable)) {
                bits |= entry_writable;
            }
            if (has_flag(flags, page_flags::user)) {
                bits |= entry_user;
            }
            if (has_flag(flags, page_flags::write_through)) {
                bits |= entry_write_through;
            }
            if (has_flag(flags, page_flags::cache_disable)) {
                bits |= entry_cache_disable;
            }
            if (has_flag(flags, page_flags::global)) {
                bits |= entry_global;
            }
            if (has_flag(flags, page_flags::no_execute)) {
                bits |= entry_no_execute;
            }
            return bits;
        }

        /// Virtual address of the table page at `level` on the path to
        /// `virt` inside the *active* map — meaningful only while that path
        /// is present, which every caller arranges (create, an already
        /// checked/created entry further up, or the self-entry). The first
        /// index must select the recursive slot — that is what enters the
        /// window — so the remaining three form a fixed pattern per level:
        ///   root: rec rec rec rec      pdpt: rec rec rec pml4(v)
        ///   pd:   rec rec pml4 pdpt    pt:   rec pml4 pdpt pd
        vaddr_t alias(table_level level, vaddr_t virt) {
            const u64 rec = recursive_slot;
            u64       a2  = rec;
            u64       a3  = rec;
            u64       a4  = rec;
            switch (level) {
            case table_level::root:
                break;
            case table_level::pdpt:
                a4 = pml4_index(virt);
                break;
            case table_level::pd:
                a3 = pml4_index(virt);
                a4 = pdpt_index(virt);
                break;
            case table_level::pt:
                a2 = pml4_index(virt);
                a3 = pdpt_index(virt);
                a4 = pd_index(virt);
                break;
            }
            return recursive_base | (a2 << 30) | (a3 << 21) | (a4 << 12);
        }

        /// A table *frame* as a pointer, for tables belonging to maps that
        /// are not active: through the kernel direct map once it runs (it is
        /// shared into every active map), through the identity window before
        /// that and in host tests.
        u64* phys_ptr(paddr_t phys) {
            if (active_map != nullptr) {
                return reinterpret_cast<u64*>(kernel_base + phys);
            }
            return reinterpret_cast<u64*>(phys);
        }

        /// `owner`'s root-table page.
        u64* root_table(const page_map& owner) {
            if (active_map == &owner) {
                return reinterpret_cast<u64*>(alias(table_level::root, 0));
            }
            return phys_ptr(owner.root());
        }

        /// The next-level table on `virt`'s walk, given the entry pointing
        /// at it (which the caller has verified present or just created).
        u64* next_table(const page_map& owner, vaddr_t virt, table_level level,
                        u64 entry) {
            if (active_map == &owner) {
                return reinterpret_cast<u64*>(alias(level, virt));
            }
            return phys_ptr(entry & address_mask);
        }

        /// Return the next-level table for `entry`, allocating and zeroing it
        /// (and setting PRESENT|RW, plus USER when requested) when absent.
        /// Returns nullptr if a huge page is in the way or allocation fails.
        u64* ensure_table(const page_map& owner, vaddr_t virt, table_level level,
                          u64& entry, page_flags flags) {
            if ((entry & entry_present) != 0) {
                if ((entry & entry_huge) != 0) {
                    return nullptr;
                }
                if (has_flag(flags, page_flags::user)) {
                    entry |= entry_user;
                }
                return next_table(owner, virt, level, entry);
            }

            const u64 frame = mm::kernel_frames().alloc(1, mm::frame_flags::zeroed);
            if (frame == mm::frame_allocator::invalid_frame) {
                return nullptr;
            }
            entry = (frame & address_mask) | entry_present | entry_writable |
                    (has_flag(flags, page_flags::user) ? entry_user : 0);
            return next_table(owner, virt, level, entry);
        }

        /// Turn a 2 MiB leaf entry into a page table full of 4 KiB entries.
        bool split_2m(const page_map& owner, vaddr_t virt, u64& pde) {
            const u64 base = pde & (~0ull << 21);
            const u64 inherited =
                pde & (entry_writable | entry_user | entry_write_through |
                       entry_cache_disable | entry_global | entry_no_execute);

            const u64 frame = mm::kernel_frames().alloc(1, mm::frame_flags::zeroed);
            if (frame == mm::frame_allocator::invalid_frame) {
                return false;
            }

            // Publish the entry first so the new table is reachable through
            // the recursive window too, then fill it.
            pde        = (frame & address_mask) | inherited | entry_present;
            u64* table = next_table(owner, virt, table_level::pt, pde);
            for (usize i = 0; i < 512; ++i) {
                table[i] =
                    (base + static_cast<u64>(i) * 4096) | inherited | entry_present;
            }
            return true;
        }

    } // namespace

    page_map::page_map(page_map&& other) noexcept
        : root_(other.root_), parent_(other.parent_) {
        other.root_   = 0;
        other.parent_ = nullptr;
    }

    page_map& page_map::operator=(page_map&& other) noexcept {
        if (this != &other) {
            root_         = other.root_;
            parent_       = other.parent_;
            other.root_   = 0;
            other.parent_ = nullptr;
        }
        return *this;
    }

    page_map page_map::create(const page_map* parent) {
        const u64 frame = mm::kernel_frames().alloc(1, mm::frame_flags::zeroed);
        if (frame == mm::frame_allocator::invalid_frame) {
            return page_map{};
        }

        page_map map(frame, parent);
        u64*     dst = phys_ptr(frame);
        if (parent != nullptr && parent->valid()) {
            // Share the parent's kernel-half top-level entries: the same
            // table frames, so the hardware walk from a child address space
            // sees every kernel mapping — including ones the kernel adds
            // after this child was created (they land in the shared tables).
            const u64* src = root_table(*parent);
            for (usize i = kernel_slot; i < 512; ++i) {
                dst[i] = src[i];
            }
        }

        // The recursive slot is never shared: each map points it back at its
        // *own* root (overwriting anything inherited above), so walking the
        // recursive window through this map addresses this map's tables.
        dst[recursive_slot] = (frame & address_mask) | entry_present | entry_writable;
        return map;
    }

    bool page_map::map_4k(vaddr_t virt, paddr_t phys, page_flags flags) {
        if (!valid() || !is_canonical(virt)) {
            return false;
        }
        if ((virt & 0xfff) != 0 || (phys & 0xfff) != 0) {
            return false;
        }
        if (pml4_index(virt) == recursive_slot) {
            return false; // reserved for the map's self-reference
        }

        u64* pml4 = root_table(*this);
        u64* pdpt =
            ensure_table(*this, virt, table_level::pdpt, pml4[pml4_index(virt)], flags);
        if (pdpt == nullptr) {
            return false;
        }
        u64* pd =
            ensure_table(*this, virt, table_level::pd, pdpt[pdpt_index(virt)], flags);
        if (pd == nullptr) {
            return false;
        }

        u64& pde = pd[pd_index(virt)];
        if ((pde & (entry_present | entry_huge)) == (entry_present | entry_huge)) {
            if (!split_2m(*this, virt, pde)) {
                return false;
            }
        }
        u64* pt = ensure_table(*this, virt, table_level::pt, pde, flags);
        if (pt == nullptr) {
            return false;
        }

        pt[pt_index(virt)] = (phys & address_mask) | to_leaf(flags);
        invalidate(virt);
        return true;
    }

    bool page_map::map_2m(vaddr_t virt, paddr_t phys, page_flags flags) {
        if (!valid() || !is_canonical(virt)) {
            return false;
        }
        if ((virt & 0x1fffff) != 0 || (phys & 0x1fffff) != 0) {
            return false;
        }
        if (pml4_index(virt) == recursive_slot) {
            return false; // reserved for the map's self-reference
        }

        u64* pml4 = root_table(*this);
        u64* pdpt =
            ensure_table(*this, virt, table_level::pdpt, pml4[pml4_index(virt)], flags);
        if (pdpt == nullptr) {
            return false;
        }

        u64& pdpe = pdpt[pdpt_index(virt)];
        if ((pdpe & (entry_present | entry_huge)) == (entry_present | entry_huge)) {
            return false; // 1 GiB page in the way; not supported.
        }
        u64* pd = ensure_table(*this, virt, table_level::pd, pdpe, flags);
        if (pd == nullptr) {
            return false;
        }

        u64& pde = pd[pd_index(virt)];
        if ((pde & entry_present) != 0 && (pde & entry_huge) == 0) {
            return false; // a 4 KiB table already covers this 2 MiB slot.
        }
        pde = (phys & (~0ull << 21)) | to_leaf(flags) | entry_huge;
        invalidate(virt);
        return true;
    }

    bool page_map::unmap(vaddr_t virt) {
        if (!valid() || !is_canonical(virt)) {
            return false;
        }
        if (pml4_index(virt) == recursive_slot) {
            return false; // reserved for the map's self-reference
        }

        const u64 pml4e = root_table(*this)[pml4_index(virt)];
        if ((pml4e & entry_present) == 0) {
            return false;
        }
        const u64 pdpe =
            next_table(*this, virt, table_level::pdpt, pml4e)[pdpt_index(virt)];
        if ((pdpe & (entry_present | entry_huge)) != entry_present) {
            return false;
        }
        const u64 pde = next_table(*this, virt, table_level::pd, pdpe)[pd_index(virt)];
        if ((pde & (entry_present | entry_huge)) != entry_present) {
            return false;
        }

        u64& pte = next_table(*this, virt, table_level::pt, pde)[pt_index(virt)];
        if ((pte & entry_present) == 0) {
            return false;
        }
        pte = 0;
        invalidate(virt);
        return true;
    }

    paddr_t page_map::translate(vaddr_t virt) const {
        if (!valid() || !is_canonical(virt)) {
            return invalid;
        }

        const u64 pml4e = root_table(*this)[pml4_index(virt)];
        if ((pml4e & entry_present) == 0) {
            // Nothing of our own here; a parent (normally the kernel map)
            // may own the address — e.g. a kernel mapping that appeared
            // after this map was created.
            return parent_ != nullptr ? parent_->translate(virt) : invalid;
        }
        const u64 pdpe =
            next_table(*this, virt, table_level::pdpt, pml4e)[pdpt_index(virt)];
        if ((pdpe & entry_present) == 0) {
            return invalid;
        }
        if ((pdpe & entry_huge) != 0) {
            return (pdpe & (~0ull << 30)) | (virt & 0x3fffffff);
        }
        const u64 pde = next_table(*this, virt, table_level::pd, pdpe)[pd_index(virt)];
        if ((pde & entry_present) == 0) {
            return invalid;
        }
        if ((pde & entry_huge) != 0) {
            return (pde & (~0ull << 21)) | (virt & 0x1fffff);
        }
        const u64 pte = next_table(*this, virt, table_level::pt, pde)[pt_index(virt)];
        if ((pte & entry_present) == 0) {
            return invalid;
        }
        return (pte & address_mask) | (virt & 0xfff);
    }

    // Host-side unit tests (tests/) compile this file directly, but they run
    // in user mode where `mov cr3` and `invlpg` are privileged — and there is
    // no real paging to recurse through, so active_map must stay null there
    // (its absence is what routes table access to physical pointers).
    void page_map::activate() const {
#ifndef ASTIA_HOST_TEST
        active_map = this;
        asm volatile("mov %0, %%cr3" : : "r"(root_) : "memory");
#endif
    }

    void page_map::invalidate(vaddr_t virt) {
#ifndef ASTIA_HOST_TEST
        asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
#else
        (void) virt;
#endif
    }

} // namespace astia::arch
