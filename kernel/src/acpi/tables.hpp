#pragma once

#include <astia/common.hpp>
#include <astia/span.hpp>

namespace astia::acpi::detail {
    constexpr usz max_tables            = 64;
    constexpr usz max_ssdts             = max_tables;
    constexpr usz max_table_bytes       = 128 * 1024;
    constexpr usz max_total_table_bytes = 512 * 1024;

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
        out_of_memory,
        source_changed,
        no_fadt,
        no_dsdt,
    };

    const char* error_text(table_error error);

    struct memory_reader {
        void* context                                                         = nullptr;
        bool (*read)(void* context, u64 address, u8* destination, usz length) = nullptr;
    };

    struct table {
        span<const u8> bytes{};
        u64            address = 0;
    };

    /// One allocation per record: the record header plus its validated table
    /// bytes. `table.value.bytes` points into the same allocation.
    struct table_record {
        table_record* next       = nullptr;
        void*         allocation = nullptr;
        u32           signature  = 0;
        u32           length     = 0;
        u8            revision   = 0;
        bool          aml        = false;
        table         value{};
    };

    struct table_allocator {
        void* context                                 = nullptr;
        void* (*allocate)(void* context, usz bytes)   = nullptr;
        void (*release)(void* context, void* pointer) = nullptr;
    };

    struct inventory_entry {
        u32  signature = 0;
        u32  length    = 0;
        u8   revision  = 0;
        bool aml       = false;
    };

    /// Owns validated copies of every discovered SDT. Records are kept in
    /// discovery order, so find(signature, occurrence) follows the same order
    /// as RSDT/XSDT enumeration and any FADT-referenced DSDT appended later.
    struct table_set {
        table_set() = default;
        ~table_set();

        table_set(const table_set&)            = delete;
        table_set& operator=(const table_set&) = delete;

        table_allocator allocator{};
        table_record*   head        = nullptr;
        table_record*   tail        = nullptr;
        usz             count       = 0;
        usz             total_bytes = 0;

        table           fadt{};
        table           dsdt{};
        table           ssdt[max_ssdts]{};
        usz             ssdt_count = 0;
        inventory_entry inventory[max_tables]{};
        usz             inventory_count = 0;
        table_error     error           = table_error::none;
        u64             rsdp_address    = 0;
        bool            valid_rsdp      = false;

        u8  root_address_space = 0;
        u32 root_signature     = 0;

        /// First record with `signature` at `occurrence` in discovery order.
        bool find(u32 signature, usz occurrence, table& out) const;
        bool find(u32 signature, usz occurrence, table_record*& out) const;

        /// Release every retained table and reset all discovery metadata.
        void clear();

        private:
        void release_records();
    };

    /// Validate the RSDP and every table reachable from its RSDT/XSDT. Table
    /// bytes are copied through `allocator`; every view in `out` points into
    /// those owned copies and stays valid until clear()/destruction.
    bool discover(const memory_reader& reader, u64 rsdp_address,
                  const table_allocator& allocator, table_set& out);

    u16 load_u16(const u8* bytes);
    u32 load_u32(const u8* bytes);
    u64 load_u64(const u8* bytes);

    inline u32 table_signature(const table& table) {
        return table.bytes.size() < 4 ? 0 : load_u32(table.bytes.data());
    }
} // namespace astia::acpi::detail
