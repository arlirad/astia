#include <astia/arch/page_map.hpp>

#include "page_map_policy.hpp"

// AArch64 stage 1 implementation of the shared page_map API. This is an
// early-boot identity map only: table frames come from one static, bounded
// arena, addresses are identity mapped, and no generic frame allocator or
// process-address-space machinery is involved.
//
// Translation uses the 4KB granule, TCR_EL1.T0SZ = 16 (48-bit lower VA range),
// and a level 0 initial lookup. The policy constants and descriptor encodings
// live in page_map_policy.hpp so the host tests exercise the same code that
// the kernel links.

namespace astia::arch {
    namespace {
        namespace policy = aarch64::page_map_policy;

        struct table_arena {
            alignas(4096) u64 entries[policy::table_capacity][policy::table_entries]{};
            usz used = 0;
        };

        table_arena arena;

        paddr_t alloc_table() {
            if (arena.used >= policy::table_capacity)
                return 0;

            u64* table = arena.entries[arena.used++];

            for (usz index = 0; index < policy::table_entries; ++index) {
                table[index] = 0;
            }

            return reinterpret_cast<paddr_t>(table);
        }

        u64* table_pointer(paddr_t phys) {
            return reinterpret_cast<u64*>(phys);
        }

        constexpr usz level0_index(vaddr_t virt) {
            return (virt >> 39) & 0x1ff;
        }

        constexpr usz level1_index(vaddr_t virt) {
            return (virt >> 30) & 0x1ff;
        }

        constexpr usz level2_index(vaddr_t virt) {
            return (virt >> 21) & 0x1ff;
        }

        constexpr usz level3_index(vaddr_t virt) {
            return (virt >> 12) & 0x1ff;
        }

        constexpr bool is_valid(u64 descriptor) {
            return (descriptor & policy::descriptor_valid) != 0;
        }

        constexpr bool is_table(u64 descriptor) {
            return (descriptor & 0x3) ==
                   (policy::descriptor_valid | policy::descriptor_table);
        }

        constexpr bool is_block(u64 descriptor) {
            return is_valid(descriptor) && !is_table(descriptor);
        }

        constexpr vaddr_t descriptor_address(u64 descriptor) {
            return descriptor & policy::descriptor_address_mask;
        }

        constexpr vaddr_t block_2m_address(u64 descriptor) {
            return descriptor & policy::descriptor_block_2m_mask;
        }

        /// Next-level table for `entry`, allocated and zeroed when the entry
        /// is absent. Returns nullptr when a block descriptor is in the way
        /// or the bounded arena is exhausted.
        u64* ensure_table(u64& entry) {
            if (is_valid(entry)) {
                if (!is_table(entry))
                    return nullptr;

                return table_pointer(descriptor_address(entry));
            }

            const paddr_t frame = alloc_table();
            if (frame == 0)
                return nullptr;

            entry = (frame & policy::descriptor_address_mask) | policy::descriptor_table |
                    policy::descriptor_valid;

            return table_pointer(frame);
        }

        /// Replace a level 2 Block descriptor with a 4KB Page table whose
        /// entries inherit the block's attributes.
        bool split_2m(u64& block) {
            const vaddr_t base = block_2m_address(block);

            const u64 inherited =
                block & ~(policy::descriptor_block_2m_mask | policy::descriptor_valid |
                          policy::descriptor_table);

            const paddr_t frame = alloc_table();
            if (frame == 0)
                return false;

            u64* table = table_pointer(frame);

            for (usz index = 0; index < policy::table_entries; ++index) {
                table[index] = (base + index * policy::page_size) | inherited |
                               policy::descriptor_valid | policy::descriptor_table;
            }

            block = (frame & policy::descriptor_address_mask) | policy::descriptor_table |
                    policy::descriptor_valid;

            return true;
        }

        enum class leaf_level {
            none,
            level1_block,
            level2_block,
            level3_page,
        };

        /// Walk one map's own tables. `root_absent` distinguishes "this map
        /// has nothing in this top-level slot" (the only case where the
        /// parent fallback applies) from a hole deeper in the walk.
        bool find_leaf(const page_map& map, vaddr_t virt, u64& leaf, leaf_level& level,
                       bool& root_absent) {
            root_absent = false;
            level       = leaf_level::none;

            if (!map.valid() || virt > policy::max_virtual_address)
                return false;

            const u64* level0 = table_pointer(map.root());

            const u64 entry0 = level0[level0_index(virt)];

            if (!is_valid(entry0)) {
                root_absent = true;

                return false;
            }

            if (!is_table(entry0)) {
                return false; // level 0 block descriptors are not supported
            }

            const u64* level1 = table_pointer(descriptor_address(entry0));

            const u64 entry1 = level1[level1_index(virt)];

            if (!is_valid(entry1))
                return false;

            if (is_block(entry1)) {
                leaf  = entry1;
                level = leaf_level::level1_block;

                return true;
            }

            const u64* level2 = table_pointer(descriptor_address(entry1));

            const u64 entry2 = level2[level2_index(virt)];

            if (!is_valid(entry2))
                return false;

            if (is_block(entry2)) {
                leaf  = entry2;
                level = leaf_level::level2_block;

                return true;
            }

            const u64* level3 = table_pointer(descriptor_address(entry2));

            const u64 entry3 = level3[level3_index(virt)];

            if (!is_valid(entry3))
                return false;

            leaf  = entry3;
            level = leaf_level::level3_page;

            return true;
        }

        constexpr vaddr_t block_offset(vaddr_t virt, leaf_level level) {
            switch (level) {
            case leaf_level::level1_block:
                return virt & 0x3fffffffULL;
            case leaf_level::level2_block:
                return virt & 0x1fffffULL;
            case leaf_level::level3_page:
                return virt & 0xfffULL;
            case leaf_level::none:
                break;
            }

            return 0;
        }

        constexpr vaddr_t block_base(u64 leaf, leaf_level level) {
            switch (level) {
            case leaf_level::level1_block:
                return leaf & policy::descriptor_block_1g_mask;
            case leaf_level::level2_block:
                return block_2m_address(leaf);
            case leaf_level::level3_page:
                return descriptor_address(leaf);
            case leaf_level::none:
                break;
            }

            return 0;
        }

        bool clone_table(const u64* source, u64* destination, usz level) {
            for (usz index = 0; index < policy::table_entries; ++index) {
                const u64 entry = source[index];

                if (!is_valid(entry)) {
                    destination[index] = 0;

                    continue;
                }

                if (level < 3 && is_table(entry)) {
                    const paddr_t frame = alloc_table();
                    if (frame == 0)
                        return false;

                    destination[index] = (frame & policy::descriptor_address_mask) |
                                         policy::descriptor_table |
                                         policy::descriptor_valid;

                    if (!clone_table(table_pointer(descriptor_address(entry)),
                                     table_pointer(frame), level + 1))
                        return false;

                    continue;
                }

                destination[index] = entry;
            }

            return true;
        }

#ifndef ASTIA_HOST_TEST
        bool mmu_active = false;

        void write_mair(u64 value) {
            __asm__ volatile("msr mair_el1, %0" : : "r"(value) : "memory");
        }

        void write_tcr(u64 value) {
            __asm__ volatile("msr tcr_el1, %0" : : "r"(value) : "memory");
        }

        void write_ttbr0(u64 value) {
            __asm__ volatile("msr ttbr0_el1, %0" : : "r"(value) : "memory");
        }

        void write_ttbr1(u64 value) {
            __asm__ volatile("msr ttbr1_el1, %0" : : "r"(value) : "memory");
        }

        u64 read_sctlr() {
            u64 value = 0;

            __asm__ volatile("mrs %0, sctlr_el1" : "=r"(value));

            return value;
        }

        void write_sctlr(u64 value) {
            __asm__ volatile("msr sctlr_el1, %0" : : "r"(value) : "memory");
        }

        void dsb_ish() {
            __asm__ volatile("dsb ish" : : : "memory");
        }

        void isb() {
            __asm__ volatile("isb" : : : "memory");
        }

        void tlbi_vmalle1is() {
            __asm__ volatile("tlbi vmalle1is" : : : "memory");
        }

        void tlbi_vae1is(vaddr_t virt) {
            const u64 operand = virt >> 12;

            __asm__ volatile("tlbi vae1is, %0" : : "r"(operand) : "memory");
        }

        constexpr u64 sctlr_m   = 1ull << 0;
        constexpr u64 sctlr_c   = 1ull << 2;
        constexpr u64 sctlr_i   = 1ull << 12;
        constexpr u64 sctlr_wxn = 1ull << 19;
        constexpr u64 sctlr_e0e = 1ull << 24;
        constexpr u64 sctlr_ee  = 1ull << 25;
#endif

        page_map kernel_map_storage;
    } // namespace

    page_map::page_map(page_map&& other) noexcept
        : _root(other._root), _parent(other._parent) {
        other._root   = 0;
        other._parent = nullptr;
    }

    page_map& page_map::operator=(page_map&& other) noexcept {
        if (this != &other) {
            _root         = other._root;
            _parent       = other._parent;
            other._root   = 0;
            other._parent = nullptr;
        }

        return *this;
    }

    page_map::~page_map() = default;

    page_map page_map::create(const page_map* parent) {
        const paddr_t frame = alloc_table();
        if (frame == 0)
            return page_map{};

        page_map map(frame, parent);

        if (parent != nullptr && parent->valid()) {
            // Early-stage parent semantics: clone the current tables. Later
            // parent updates are not shared; process address spaces are out
            // of scope for this static arena.
            if (!clone_table(table_pointer(parent->root()), table_pointer(frame), 0))
                return page_map{};
        }

        return map;
    }

    bool page_map::map_4k(vaddr_t virt, paddr_t phys, page_flags flags) {
        if (!valid() || !policy::page_fits(virt) || !policy::page_physical_fits(phys))
            return false;

        u64* level0 = table_pointer(_root);
        u64* level1 = ensure_table(level0[level0_index(virt)]);
        if (level1 == nullptr)
            return false;

        u64* level2 = ensure_table(level1[level1_index(virt)]);
        if (level2 == nullptr)
            return false;

        u64& entry2 = level2[level2_index(virt)];

        if (is_valid(entry2) && !is_table(entry2)) {
            if (!split_2m(entry2))
                return false;
        }

        u64* level3 = ensure_table(entry2);
        if (level3 == nullptr)
            return false;

        level3[level3_index(virt)] = (phys & policy::descriptor_address_mask) |
                                     policy::leaf_attributes(flags, true);

        invalidate(virt);

        return true;
    }

    bool page_map::map_2m(vaddr_t virt, paddr_t phys, page_flags flags) {
        if (!valid() || !policy::block_fits(virt) || !policy::block_physical_fits(phys))
            return false;

        u64* level0 = table_pointer(_root);
        u64* level1 = ensure_table(level0[level0_index(virt)]);
        if (level1 == nullptr)
            return false;

        u64* level2 = ensure_table(level1[level1_index(virt)]);
        if (level2 == nullptr)
            return false;

        u64& entry2 = level2[level2_index(virt)];

        if (is_valid(entry2) && is_table(entry2))
            return false;

        entry2 = (phys & policy::descriptor_block_2m_mask) |
                 policy::leaf_attributes(flags, false);

        invalidate(virt);

        return true;
    }

    bool page_map::unmap(vaddr_t virt) {
        if (!valid() || virt > policy::max_virtual_address)
            return false;

        u64 leaf = 0;

        leaf_level level = leaf_level::none;

        bool root_absent = false;

        if (!find_leaf(*this, virt, leaf, level, root_absent) ||
            level != leaf_level::level3_page)
            return false;

        const u64* level0 = table_pointer(_root);

        const u64 entry0 = level0[level0_index(virt)];

        u64* level1 = table_pointer(descriptor_address(entry0));

        const u64 entry1 = level1[level1_index(virt)];

        u64* level2 = table_pointer(descriptor_address(entry1));

        const u64 entry2 = level2[level2_index(virt)];

        u64* level3 = table_pointer(descriptor_address(entry2));

        level3[level3_index(virt)] = 0;

        invalidate(virt);

        return true;
    }

    bool page_map::protect_4k(vaddr_t virt, page_flags flags) {
        if (!valid() || virt > policy::max_virtual_address ||
            (virt & (policy::page_size - 1)) != 0)
            return false;

        u64 leaf = 0;

        leaf_level level = leaf_level::none;

        bool root_absent = false;

        if (!find_leaf(*this, virt, leaf, level, root_absent) ||
            level != leaf_level::level3_page)
            return false;

        const u64* level0 = table_pointer(_root);

        const u64 entry0 = level0[level0_index(virt)];

        u64* level1 = table_pointer(descriptor_address(entry0));

        const u64 entry1 = level1[level1_index(virt)];

        u64* level2 = table_pointer(descriptor_address(entry1));

        const u64 entry2 = level2[level2_index(virt)];

        u64* level3 = table_pointer(descriptor_address(entry2));

        level3[level3_index(virt)] = (leaf & policy::descriptor_address_mask) |
                                     policy::leaf_attributes(flags, true);

        invalidate(virt);

        return true;
    }

    paddr_t page_map::translate(vaddr_t virt) const {
        u64 leaf = 0;

        leaf_level level = leaf_level::none;

        bool root_absent = false;

        if (!find_leaf(*this, virt, leaf, level, root_absent)) {
            if (root_absent && _parent != nullptr)
                return _parent->translate(virt);

            return invalid;
        }

        return block_base(leaf, level) + block_offset(virt, level);
    }

    bool page_map::writable(vaddr_t virt) const {
        u64 leaf = 0;

        leaf_level level = leaf_level::none;

        bool root_absent = false;

        if (!find_leaf(*this, virt, leaf, level, root_absent))
            return root_absent && _parent != nullptr && _parent->writable(virt);

        return (leaf & policy::descriptor_ap_ro) == 0;
    }

    bool page_map::user_accessible(vaddr_t virt) const {
        u64 leaf = 0;

        leaf_level level = leaf_level::none;

        bool root_absent = false;

        if (!find_leaf(*this, virt, leaf, level, root_absent))
            return root_absent && _parent != nullptr && _parent->user_accessible(virt);

        return (leaf & policy::descriptor_ap_el0) != 0;
    }

    /// This backend reports EL1 instruction-execution permission (PXN == 0).
    /// The shared API does not prescribe a privilege level; x86 reports the
    /// user-executable permission for its own mappings.
    bool page_map::executable(vaddr_t virt) const {
        u64 leaf = 0;

        leaf_level level = leaf_level::none;

        bool root_absent = false;

        if (!find_leaf(*this, virt, leaf, level, root_absent))
            return root_absent && _parent != nullptr && _parent->executable(virt);

        return (leaf & policy::descriptor_pxn) == 0;
    }

    void page_map::activate() const {
#ifndef ASTIA_HOST_TEST
        if (!valid())
            return;

        write_mair(policy::mair_value);
        write_tcr(policy::tcr_value);
        write_ttbr0(_root);
        write_ttbr1(0);

        dsb_ish();
        isb();

        tlbi_vmalle1is();
        dsb_ish();
        isb();

        u64 sctlr = read_sctlr();

        sctlr |= sctlr_m | sctlr_c | sctlr_i;
        sctlr &= ~(sctlr_wxn | sctlr_e0e | sctlr_ee);

        write_sctlr(sctlr);
        isb();

        mmu_active = true;
#endif
    }

    void page_map::invalidate(vaddr_t virt) {
#ifndef ASTIA_HOST_TEST
        if (!mmu_active || virt > policy::max_virtual_address)
            return;

        // Order the descriptor write before the invalidate, then make the
        // invalidation completion visible to subsequent accesses.
        dsb_ish();
        tlbi_vae1is(virt);
        dsb_ish();
        isb();
#else
        (void) virt;
#endif
    }

    page_map* kernel_page_map() {
        return &kernel_map_storage;
    }
} // namespace astia::arch
