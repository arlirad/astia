#include "tables.hpp"

namespace astia::acpi::detail {
    namespace {
        constexpr u64 signature_rsdp = 0x2052545020445352ull; // "RSD PTR "

        bool checked_end(u64 address, usize length) {
            return address <= ~u64{0} - length;
        }

        bool read_bytes(const memory_reader& reader, u64 address, u8* destination,
                        usize length) {
            if (reader.read == nullptr || destination == nullptr ||
                !checked_end(address, length))
                return false;

            return reader.read(reader.context, address, destination, length);
        }

        u8 checksum(const u8* bytes, usize length) {
            u8 sum = 0;

            for (usize i = 0; i < length; ++i) {
                sum = static_cast<u8>(sum + bytes[i]);
            }

            return sum;
        }

        bool rsdp_signature_ok(const u8* bytes) {
            return load_u64(bytes) == signature_rsdp;
        }

        bool checksum_range(const memory_reader& reader, u64 address, usize length) {
            u8    chunk[64]{};
            u8    sum    = 0;
            usize offset = 0;

            while (offset < length) {
                const usize count =
                    length - offset < sizeof(chunk) ? length - offset : sizeof(chunk);

                if (!read_bytes(reader, address + offset, chunk, count))
                    return false;

                for (usize i = 0; i < count; ++i) {
                    sum = static_cast<u8>(sum + chunk[i]);
                }

                offset += count;
            }

            return sum == 0;
        }

        struct rsdp_info {
            u32 revision = 0;
            u32 length   = rsdp_v1_size;
            u64 rsdt     = 0;
            u64 xsdt     = 0;
        };

        bool validate_rsdp(const memory_reader& reader, u64 address, rsdp_info& info) {
            u8 bytes[rsdp_v2_size]{};

            if (address == 0)
                return false;

            if (!read_bytes(reader, address, bytes, rsdp_v1_size))
                return false;

            if (!rsdp_signature_ok(bytes))
                return false;

            if (checksum(bytes, rsdp_v1_size) != 0)
                return false;

            info.revision = bytes[15];
            info.rsdt     = load_u32(bytes + 16);

            if (info.revision < 2)
                return true;

            if (!read_bytes(reader, address, bytes, rsdp_v2_size))
                return false;

            info.length = load_u32(bytes + 20);

            if (info.length < rsdp_v2_size || info.length > max_rsdp_length)
                return false;

            if (!checksum_range(reader, address, info.length))
                return false;

            info.xsdt = load_u64(bytes + 24);

            return true;
        }
    } // namespace

    u16 load_u16(const u8* bytes) {
        return static_cast<u16>(bytes[0]) | (static_cast<u16>(bytes[1]) << 8);
    }

    u32 load_u32(const u8* bytes) {
        return static_cast<u32>(bytes[0]) | (static_cast<u32>(bytes[1]) << 8) |
               (static_cast<u32>(bytes[2]) << 16) | (static_cast<u32>(bytes[3]) << 24);
    }

    u64 load_u64(const u8* bytes) {
        u64 value = 0;

        for (usize i = 0; i < 8; ++i) {
            value |= static_cast<u64>(bytes[i]) << (i * 8);
        }

        return value;
    }

    const char* error_text(table_error error) {
        switch (error) {
        case table_error::none:
            return "ok";
        case table_error::no_rsdp:
            return "RSDP absent";
        case table_error::bad_rsdp_signature:
            return "RSDP signature";
        case table_error::bad_rsdp_checksum:
            return "RSDP checksum";
        case table_error::bad_rsdp_length:
            return "RSDP length";
        case table_error::bad_root_table:
            return "root table";
        case table_error::bad_table_signature:
            return "table signature";
        case table_error::bad_table_length:
            return "table length";
        case table_error::bad_table_checksum:
            return "table checksum";
        case table_error::truncated:
            return "truncated table";
        case table_error::duplicate_table:
            return "duplicate table";
        case table_error::too_many_tables:
            return "table count";
        case table_error::too_large:
            return "table too large";
        case table_error::storage_exhausted:
            return "table storage";
        case table_error::no_fadt:
            return "FADT absent";
        case table_error::no_dsdt:
            return "DSDT absent";
        }

        return "unknown";
    }

    bool discover(const memory_reader& reader, u64 rsdp_address, span<u8> storage,
                  table_set& out) {
        out              = table_set{};
        out.rsdp_address = rsdp_address;

        if (rsdp_address == 0) {
            out.error = table_error::no_rsdp;

            return false;
        }

        rsdp_info rsdp{};

        if (!validate_rsdp(reader, rsdp_address, rsdp)) {
            u8 header[rsdp_v1_size]{};

            if (!read_bytes(reader, rsdp_address, header, rsdp_v1_size)) {
                out.error = table_error::no_rsdp;
            }
            else if (!rsdp_signature_ok(header)) {
                out.error = table_error::bad_rsdp_signature;
            }
            else if (checksum(header, rsdp_v1_size) != 0) {
                out.error = table_error::bad_rsdp_checksum;
            }
            else {
                out.error = table_error::bad_rsdp_length;
            }

            return false;
        }

        out.valid_rsdp = true;

        const u64 root_address = rsdp.xsdt != 0 ? rsdp.xsdt : rsdp.rsdt;

        if (root_address == 0) {
            out.error = table_error::bad_root_table;

            return false;
        }

        u8 root_header[sdt_header_size]{};

        if (!read_bytes(reader, root_address, root_header, sdt_header_size)) {
            out.error = table_error::truncated;

            return false;
        }

        const u32 root_signature = load_u32(root_header);
        const u32 root_length    = load_u32(root_header + 4);

        if (root_signature != xsdt_signature && root_signature != rsdt_signature) {
            out.error = table_error::bad_root_table;

            return false;
        }

        if (root_length < sdt_header_size || root_length > max_table_bytes) {
            out.error = table_error::bad_table_length;

            return false;
        }

        if (root_length > storage.size()) {
            out.error = table_error::storage_exhausted;

            return false;
        }

        u8* root_bytes = storage.data();
        if (!read_bytes(reader, root_address, root_bytes, root_length)) {
            out.error = table_error::truncated;

            return false;
        }

        if (checksum(root_bytes, root_length) != 0) {
            out.error = table_error::bad_table_checksum;

            return false;
        }

        const usize entry_size = root_signature == xsdt_signature ? 8 : 4;

        if ((root_length - sdt_header_size) % entry_size != 0) {
            out.error = table_error::bad_table_length;

            return false;
        }

        const usize entry_count = (root_length - sdt_header_size) / entry_size;

        if (entry_count + 1 > max_tables) {
            out.error = table_error::too_many_tables;

            return false;
        }

        out.root_signature     = root_signature;
        out.root_address_space = rsdp.rsdt != 0 ? 1 : 2;

        usize used = root_length;

        out.inventory[out.inventory_count++] = {
            root_signature,
            root_length,
            root_header[8],
            false,
        };

        u64 table_addresses[max_tables]{};

        usize table_address_count = 1;

        table_addresses[0] = root_address;

        for (usize i = 0; i < entry_count; ++i) {
            const u8* entry   = root_bytes + sdt_header_size + i * entry_size;
            const u64 address = entry_size == 8 ? load_u64(entry) : load_u32(entry);

            if (address == 0) {
                out.error = table_error::bad_root_table;

                return false;
            }

            for (usize k = 0; k < table_address_count; ++k) {
                if (table_addresses[k] == address) {
                    out.error = table_error::duplicate_table;

                    return false;
                }
            }

            if (table_address_count == max_tables) {
                out.error = table_error::too_many_tables;

                return false;
            }

            table_addresses[table_address_count++] = address;

            u8 header[sdt_header_size]{};

            if (!read_bytes(reader, address, header, sdt_header_size)) {
                out.error = table_error::truncated;

                return false;
            }

            const u32 table_length = load_u32(header + 4);

            if (table_length < sdt_header_size || table_length > max_table_bytes) {
                out.error = table_error::bad_table_length;

                return false;
            }

            if (used > storage.size() || table_length > storage.size() - used) {
                out.error = table_error::storage_exhausted;

                return false;
            }

            u8* bytes = storage.data() + used;

            if (!read_bytes(reader, address, bytes, table_length)) {
                out.error = table_error::truncated;

                return false;
            }

            if (checksum(bytes, table_length) != 0) {
                out.error = table_error::bad_table_checksum;

                return false;
            }

            const u32 table_signature = load_u32(bytes);

            if (table_signature == 0) {
                out.error = table_error::bad_table_signature;

                return false;
            }

            const table loaded{
                span<const u8>(bytes, table_length),
                address,
            };

            if (out.inventory_count == max_tables) {
                out.error = table_error::too_many_tables;

                return false;
            }

            switch (table_signature) {
            case fadt_signature:
                out.fadt = loaded;

                break;
            case dsdt_signature:
                out.dsdt = loaded;

                break;
            case ssdt_signature:
                if (out.ssdt_count == max_ssdts) {
                    out.error = table_error::too_many_tables;

                    return false;
                }

                out.ssdt[out.ssdt_count++] = loaded;

                break;
            default:
                break;
            }

            out.inventory[out.inventory_count++] = {
                table_signature,
                table_length,
                header[8],
                table_signature == dsdt_signature || table_signature == ssdt_signature,
            };
            used += table_length;

            if (used > max_aml_bytes) {
                out.error = table_error::too_large;

                return false;
            }
        }

        if (out.fadt.bytes.empty()) {
            out.error = table_error::no_fadt;

            return false;
        }

        if (out.dsdt.bytes.empty()) {
            const u8* fadt = out.fadt.bytes.data();

            if (out.fadt.bytes.size() < 44) {
                out.error = table_error::bad_table_length;

                return false;
            }

            u64 dsdt_address = load_u32(fadt + 40);

            if (out.fadt.bytes.size() >= 148) {
                const u64 extended = load_u64(fadt + 140);

                if (extended != 0)
                    dsdt_address = extended;
            }

            if (dsdt_address == 0) {
                out.error = table_error::no_dsdt;

                return false;
            }

            for (usize k = 0; k < table_address_count; ++k) {
                if (table_addresses[k] == dsdt_address) {
                    out.error = table_error::duplicate_table;

                    return false;
                }
            }

            u8 header[sdt_header_size]{};

            if (!read_bytes(reader, dsdt_address, header, sdt_header_size)) {
                out.error = table_error::truncated;

                return false;
            }

            const u32 table_length = load_u32(header + 4);

            if (table_length < sdt_header_size || table_length > max_table_bytes) {
                out.error = table_error::bad_table_length;

                return false;
            }

            if (used > storage.size() || table_length > storage.size() - used) {
                out.error = table_error::storage_exhausted;

                return false;
            }

            u8* bytes = storage.data() + used;

            if (!read_bytes(reader, dsdt_address, bytes, table_length)) {
                out.error = table_error::truncated;

                return false;
            }

            if (checksum(bytes, table_length) != 0) {
                out.error = table_error::bad_table_checksum;

                return false;
            }

            if (load_u32(bytes) != dsdt_signature) {
                out.error = table_error::bad_table_signature;

                return false;
            }

            if (out.inventory_count == max_tables) {
                out.error = table_error::too_many_tables;

                return false;
            }

            out.dsdt = {span<const u8>(bytes, table_length), dsdt_address};
            out.inventory[out.inventory_count++] = {
                dsdt_signature,
                table_length,
                header[8],
                true,
            };
        }

        out.error = table_error::none;

        return true;
    }
} // namespace astia::acpi::detail
