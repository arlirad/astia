#pragma once

#include <astia/arch/page_map.hpp>

namespace astia::arch::aarch64::page_map_policy {

    // MAIR_EL1.Attr<n> encodings. Attr0/Attr1 are the required Device and
    // Normal WBWA indices; Attr2/Attr3 honor the shared cache_disable and
    // write_through flags instead of silently ignoring them.
    inline constexpr u64 mair_device_ngnre = 0x04;
    inline constexpr u64 mair_normal_wbwa  = 0xff;
    inline constexpr u64 mair_normal_nc    = 0x44;
    inline constexpr u64 mair_normal_wt    = 0xaa;
    inline constexpr u64 mair_value = (mair_normal_wt << 24) | (mair_normal_nc << 16) |
                                      (mair_normal_wbwa << 8) | mair_device_ngnre;

    // TCR_EL1: 4KB granule, 48-bit lower VA range, 40-bit PA policy,
    // Inner Shareable and WBWA translation-table walks. TTBR1 is disabled.
    inline constexpr u64 tcr_t0sz_48bit  = 16;
    inline constexpr u64 tcr_t1sz_unused = 16;
    inline constexpr u64 tcr_irgn0_wbwa  = 1ull << 8;
    inline constexpr u64 tcr_orgn0_wbwa  = 1ull << 10;
    inline constexpr u64 tcr_sh0_inner   = 3ull << 12;
    inline constexpr u64 tcr_tg0_4k      = 0ull << 14;
    inline constexpr u64 tcr_epd1        = 1ull << 23;
    inline constexpr u64 tcr_ips_40bit   = 2ull << 32;
    inline constexpr u64 tcr_value = tcr_t0sz_48bit | (tcr_t1sz_unused << 16) | tcr_epd1 |
                                     tcr_irgn0_wbwa | tcr_orgn0_wbwa | tcr_sh0_inner |
                                     tcr_tg0_4k | tcr_ips_40bit;

    inline constexpr u64 page_size            = 4096;
    inline constexpr u64 block_size_2m        = 2ull * 1024 * 1024;
    inline constexpr u64 max_virtual_address  = (1ull << 48) - 1;
    inline constexpr u64 max_physical_address = (1ull << 40) - 1;

    inline constexpr usize table_capacity = 256;
    inline constexpr usize table_entries  = 512;
    inline constexpr u64   table_bytes    = table_entries * sizeof(u64);

    inline constexpr u64 descriptor_valid    = 1ull << 0;
    inline constexpr u64 descriptor_table    = 1ull << 1;
    inline constexpr u64 descriptor_af       = 1ull << 10;
    inline constexpr u64 descriptor_ap_ro    = 1ull << 7;
    inline constexpr u64 descriptor_ap_el0   = 1ull << 6;
    inline constexpr u64 descriptor_ng       = 1ull << 11;
    inline constexpr u64 descriptor_sh_inner = 3ull << 8;
    inline constexpr u64 descriptor_pxn      = 1ull << 53;
    inline constexpr u64 descriptor_uxn      = 1ull << 54;

    inline constexpr u64 descriptor_address_mask  = 0x0000FFFFFFFFF000ull;
    inline constexpr u64 descriptor_block_1g_mask = 0x0000FFFFC0000000ull;
    inline constexpr u64 descriptor_block_2m_mask = 0x0000FFFFFFE00000ull;

    enum class memory_kind : u8 {
        normal_wbwa,
        normal_wt,
        normal_nc,
        device,
    };

    constexpr memory_kind kind_for(page_flags flags) {
        if (has_flag(flags, page_flags::device)) {
            return memory_kind::device;
        }
        if (has_flag(flags, page_flags::cache_disable)) {
            return memory_kind::normal_nc;
        }
        if (has_flag(flags, page_flags::write_through)) {
            return memory_kind::normal_wt;
        }
        return memory_kind::normal_wbwa;
    }

    constexpr u64 attr_index_for(page_flags flags) {
        switch (kind_for(flags)) {
        case memory_kind::device:
            return 0;
        case memory_kind::normal_nc:
            return 2;
        case memory_kind::normal_wt:
            return 3;
        case memory_kind::normal_wbwa:
            break;
        }
        return 1;
    }

    constexpr bool is_cacheable_normal(page_flags flags) {
        const memory_kind kind = kind_for(flags);
        return kind == memory_kind::normal_wbwa || kind == memory_kind::normal_wt;
    }

    /// Leaf/Block descriptor attribute bits for a stage 1 mapping. `page`
    /// selects bit[1] = 1 (level 3 Page) instead of 0 (level 2 Block).
    constexpr u64 leaf_attributes(page_flags flags, bool page) {
        u64 bits = descriptor_valid | descriptor_af;

        if (page) {
            bits |= descriptor_table;
        }
        if (!has_flag(flags, page_flags::writable)) {
            bits |= descriptor_ap_ro;
        }
        if (has_flag(flags, page_flags::user)) {
            bits |= descriptor_ap_el0 | descriptor_ng;
        }
        bits |= attr_index_for(flags) << 2;
        if (is_cacheable_normal(flags)) {
            bits |= descriptor_sh_inner;
        }

        const bool device = kind_for(flags) == memory_kind::device;
        if (has_flag(flags, page_flags::no_execute) || device) {
            bits |= descriptor_pxn | descriptor_uxn;
        }
        else if (!has_flag(flags, page_flags::user)) {
            // Executable at EL1, never executable at EL0.
            bits |= descriptor_uxn;
        }
        return bits;
    }

    constexpr bool page_fits(u64 address) {
        return (address & (page_size - 1)) == 0 &&
               address <= max_virtual_address - (page_size - 1);
    }

    constexpr bool page_physical_fits(u64 address) {
        return (address & (page_size - 1)) == 0 &&
               address <= max_physical_address - (page_size - 1);
    }

    constexpr bool block_fits(u64 address) {
        return (address & (block_size_2m - 1)) == 0 &&
               address <= max_virtual_address - (block_size_2m - 1);
    }

    constexpr bool block_physical_fits(u64 address) {
        return (address & (block_size_2m - 1)) == 0 &&
               address <= max_physical_address - (block_size_2m - 1);
    }

} // namespace astia::arch::aarch64::page_map_policy
