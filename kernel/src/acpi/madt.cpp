#include "madt.hpp"

#include "limits.hpp"

namespace astia::acpi::detail {
    namespace {
        constexpr u64 rsdp_signature = 0x2052545020445352ull; // "RSD PTR "
        constexpr u32 madt_signature = signature("APIC");
        constexpr u32 xsdt_signature = signature("XSDT");
        constexpr u32 rsdt_signature = signature("RSDT");

        constexpr usz sdt_length_offset      = 4;
        constexpr usz rsdp_revision_offset   = 15;
        constexpr usz rsdp_rsdt_offset       = 16;
        constexpr usz rsdp_length_offset     = 20;
        constexpr usz rsdp_xsdt_offset       = 24;
        constexpr usz mad_header_size        = sdt_header_size + 8;
        constexpr u8  processor_local_apic   = 0;
        constexpr u8  processor_local_x2apic = 9;
        constexpr u32 apic_enabled           = 1;

        u32 load32(const u8* bytes) {
            return static_cast<u32>(bytes[0]) | (static_cast<u32>(bytes[1]) << 8) |
                   (static_cast<u32>(bytes[2]) << 16) |
                   (static_cast<u32>(bytes[3]) << 24);
        }

        u64 load64(const u8* bytes) {
            u64 value = 0;

            for (usz i = 0; i < 8; ++i) {
                value |= static_cast<u64>(bytes[i]) << (i * 8);
            }

            return value;
        }

        bool checked_end(u64 address, usz length) {
            return address <= ~u64{0} - length;
        }

        bool read_exact(const memory_reader& reader, u64 address, u8* destination,
                        usz length) {
            if (reader.read == nullptr || destination == nullptr ||
                !checked_end(address, length))
                return false;

            return reader.read(reader.context, address, destination, length);
        }

        bool checksum_ok(const memory_reader& reader, u64 address, usz length) {
            u8  chunk[64]{};
            u8  sum    = 0;
            usz offset = 0;

            while (offset < length) {
                const usz count =
                    length - offset < sizeof(chunk) ? length - offset : sizeof(chunk);

                u64 current = 0;

                if (!checked_add(address, offset, current) ||
                    !read_exact(reader, current, chunk, count))
                    return false;

                for (usz i = 0; i < count; ++i) {
                    sum = static_cast<u8>(sum + chunk[i]);
                }

                offset += count;
            }

            return sum == 0;
        }

        bool valid_table_length(u32 length, usz minimum) {
            return length >= minimum && length <= max_table_bytes;
        }

        bool add_apic_id(u32* apic_ids, u32 capacity, u32& count, u32 id,
                         madt_error& error) {
            for (u32 i = 0; i < count; ++i) {
                if (apic_ids[i] == id) {
                    error = madt_error::duplicate_apic_id;

                    return false;
                }
            }

            if (count == capacity) {
                error = madt_error::too_many_cpus;

                return false;
            }

            apic_ids[count++] = id;

            return true;
        }

        bool parse_madt_entries(const memory_reader& reader, u64 address, u32 length,
                                u32* apic_ids, u32 capacity, u32& count,
                                madt_error& error) {
            if (!checksum_ok(reader, address, length)) {
                error = madt_error::bad_table_checksum;

                return false;
            }

            usz offset = mad_header_size;

            while (offset < length) {
                if (length - offset < 2) {
                    error = madt_error::bad_entry;

                    return false;
                }

                u8  entry[16]{};
                u64 entry_address = 0;

                if (!checked_add(address, offset, entry_address) ||
                    !read_exact(reader, entry_address, entry, 2)) {
                    error = madt_error::bad_entry;

                    return false;
                }

                const u8 type         = entry[0];
                const u8 entry_length = entry[1];

                if (entry_length < 2 || entry_length > length - offset) {
                    error = madt_error::bad_entry;

                    return false;
                }

                if (type == processor_local_apic) {
                    if (entry_length < 8) {
                        error = madt_error::bad_entry;

                        return false;
                    }

                    if (!read_exact(reader, entry_address, entry, 8)) {
                        error = madt_error::bad_entry;

                        return false;
                    }

                    if ((load32(entry + 4) & apic_enabled) != 0 &&
                        !add_apic_id(apic_ids, capacity, count, entry[3], error))
                        return false;
                }
                else if (type == processor_local_x2apic) {
                    if (entry_length < 16) {
                        error = madt_error::bad_entry;

                        return false;
                    }

                    if (!read_exact(reader, entry_address, entry, 16)) {
                        error = madt_error::bad_entry;

                        return false;
                    }

                    if ((load32(entry + 8) & apic_enabled) != 0 &&
                        !add_apic_id(apic_ids, capacity, count, load32(entry + 4), error))
                        return false;
                }

                offset += entry_length;
            }

            if (count == 0) {
                error = madt_error::no_cpus;

                return false;
            }

            return true;
        }

        enum class walk_result : u8 {
            absent,
            found,
            malformed,
        };

        walk_result walk_root(const memory_reader& reader, u64 address, bool extended,
                              u32* apic_ids, u32 capacity, u32& count,
                              madt_error& error) {
            u8 header[sdt_header_size]{};

            if (address == 0 || !read_exact(reader, address, header, sdt_header_size)) {
                error = madt_error::bad_root_table;

                return walk_result::malformed;
            }

            const u32 expected_signature = extended ? xsdt_signature : rsdt_signature;
            const u32 length             = load32(header + sdt_length_offset);

            if (load32(header) != expected_signature ||
                !valid_table_length(length, sdt_header_size)) {
                error = load32(header) != expected_signature
                            ? madt_error::bad_root_table
                            : madt_error::bad_table_length;

                return walk_result::malformed;
            }

            const usz entry_size = extended ? 8 : 4;

            if ((length - sdt_header_size) % entry_size != 0) {
                error = madt_error::bad_root_table;

                return walk_result::malformed;
            }

            const usz entry_count = (length - sdt_header_size) / entry_size;
            if (entry_count > max_tables) {
                error = madt_error::bad_root_table;

                return walk_result::malformed;
            }

            if (!checksum_ok(reader, address, length)) {
                error = madt_error::bad_table_checksum;

                return walk_result::malformed;
            }

            for (usz i = 0; i < entry_count; ++i) {
                u8  entry[8]{};
                u64 entry_address = 0;

                if (!checked_add(address, sdt_header_size + i * entry_size,
                                 entry_address) ||
                    !read_exact(reader, entry_address, entry, entry_size)) {
                    error = madt_error::bad_root_table;

                    return walk_result::malformed;
                }

                const u64 table_address = extended ? load64(entry) : load32(entry);
                if (table_address == 0) {
                    error = madt_error::bad_root_table;

                    return walk_result::malformed;
                }

                u8 table_header[sdt_header_size]{};

                if (!read_exact(reader, table_address, table_header, sdt_header_size)) {
                    error = madt_error::bad_root_table;

                    return walk_result::malformed;
                }

                if (load32(table_header) != madt_signature)
                    continue;

                const u32 table_length = load32(table_header + sdt_length_offset);

                if (!valid_table_length(table_length, mad_header_size)) {
                    error = madt_error::bad_table_length;

                    return walk_result::malformed;
                }

                if (!parse_madt_entries(reader, table_address, table_length, apic_ids,
                                        capacity, count, error))
                    return walk_result::malformed;

                return walk_result::found;
            }

            return walk_result::absent;
        }

        struct rsdp_info {
            u8  revision = 0;
            u32 rsdt     = 0;
            u64 xsdt     = 0;
        };

        bool validate_rsdp(const memory_reader& reader, u64 address, rsdp_info& info,
                           madt_error& error) {
            u8 bytes[rsdp_v2_size]{};

            if (address == 0 || !read_exact(reader, address, bytes, rsdp_v1_size)) {
                error = madt_error::bad_rsdp;

                return false;
            }

            if (load64(bytes) != rsdp_signature ||
                !checksum_ok(reader, address, rsdp_v1_size)) {
                error = madt_error::bad_rsdp;

                return false;
            }

            info.revision = bytes[rsdp_revision_offset];
            info.rsdt     = load32(bytes + rsdp_rsdt_offset);

            if (info.revision < 2)
                return true;

            if (!read_exact(reader, address, bytes, rsdp_v2_size)) {
                error = madt_error::bad_rsdp;

                return false;
            }

            const u32 length = load32(bytes + rsdp_length_offset);

            if (length < rsdp_v2_size || length > max_rsdp_length ||
                !checksum_ok(reader, address, length)) {
                error = madt_error::bad_rsdp;

                return false;
            }

            info.xsdt = load64(bytes + rsdp_xsdt_offset);

            return true;
        }
    } // namespace

    bool discover_madt(const memory_reader& reader, u64 rsdp_address, u32* apic_ids,
                       u32 capacity, u32& count, madt_error& error) {
        count = 0;
        error = madt_error::none;

        if (apic_ids == nullptr || capacity == 0) {
            error = madt_error::too_many_cpus;

            return false;
        }

        rsdp_info rsdp{};

        if (!validate_rsdp(reader, rsdp_address, rsdp, error))
            return false;

        if (rsdp.revision >= 2 && rsdp.xsdt != 0) {
            const walk_result result =
                walk_root(reader, rsdp.xsdt, true, apic_ids, capacity, count, error);

            if (result == walk_result::found)
                return true;

            if (result == walk_result::malformed) {
                count = 0;

                return false;
            }
        }

        if (rsdp.rsdt != 0) {
            const walk_result result =
                walk_root(reader, rsdp.rsdt, false, apic_ids, capacity, count, error);

            if (result == walk_result::found)
                return true;

            if (result == walk_result::malformed) {
                count = 0;

                return false;
            }
        }

        count = 0;
        error = madt_error::no_madt;

        return false;
    }
} // namespace astia::acpi::detail
