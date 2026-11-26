#pragma once

#include <astia/common.hpp>
#include <astia/span.hpp>

namespace astia::acpi::detail {
    constexpr usize max_tables      = 64;
    constexpr usize max_ssdts       = 32;
    constexpr usize max_table_bytes = 128 * 1024;
    constexpr usize max_aml_bytes   = 512 * 1024;

    constexpr u32 rsdp_v1_size    = 20;
    constexpr u32 rsdp_v2_size    = 36;
    constexpr u32 sdt_header_size = 36;
    constexpr u32 max_rsdp_length = 4096;

    constexpr u32 signature(const char (&text)[5]) {
        return static_cast<u32>(static_cast<u8>(text[0])) |
               (static_cast<u32>(static_cast<u8>(text[1])) << 8) |
               (static_cast<u32>(static_cast<u8>(text[2])) << 16) |
               (static_cast<u32>(static_cast<u8>(text[3])) << 24);
    }

    inline constexpr u32 rsdp_signature = signature("RSD ");
    inline constexpr u32 rsdt_signature = signature("RSDT");
    inline constexpr u32 xsdt_signature = signature("XSDT");
    inline constexpr u32 fadt_signature = signature("FACP");
    inline constexpr u32 dsdt_signature = signature("DSDT");
    inline constexpr u32 ssdt_signature = signature("SSDT");

    enum class table_error : u8 {
        none,
        no_rsdp,
        bad_rsdp_signature,
        bad_rsdp_checksum,
        bad_rsdp_length,
        bad_root_table,
        bad_table_signature,
        bad_table_length,
        bad_table_checksum,
        truncated,
        duplicate_table,
        too_many_tables,
        too_large,
        storage_exhausted,
        no_fadt,
        no_dsdt,
    };

    const char* error_text(table_error error);

    struct memory_reader {
        void* context                                                           = nullptr;
        bool (*read)(void* context, u64 address, u8* destination, usize length) = nullptr;
    };

    struct table {
        span<const u8> bytes{};
        u64            address = 0;
    };

    struct inventory_entry {
        u32  signature = 0;
        u32  length    = 0;
        u8   revision  = 0;
        bool aml       = false;
    };

    struct table_set {
        table           fadt{};
        table           dsdt{};
        table           ssdt[max_ssdts]{};
        usize           ssdt_count = 0;
        inventory_entry inventory[max_tables]{};
        usize           inventory_count = 0;
        table_error     error           = table_error::none;
        u64             rsdp_address    = 0;
        bool            valid_rsdp      = false;

        u8  root_address_space = 0;
        u32 root_signature     = 0;
    };

    /// Validate the RSDP and every table reachable from its RSDT/XSDT.  Table
    /// bytes are copied into `storage`; every view in `out` points there and
    /// therefore stays valid for as long as `storage` does.
    bool discover(const memory_reader& reader, u64 rsdp_address, span<u8> storage,
                  table_set& out);

    u16 load_u16(const u8* bytes);
    u32 load_u32(const u8* bytes);
    u64 load_u64(const u8* bytes);

    inline u32 table_signature(const table& table) {
        return table.bytes.size() < 4 ? 0 : load_u32(table.bytes.data());
    }
} // namespace astia::acpi::detail
