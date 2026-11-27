#include "tables.hpp"

#include <type_traits>

#include <new>

namespace astia::acpi::detail {
    namespace {
        constexpr u64 signature_rsdp = 0x2052545020445352ull; // "RSD PTR "

        bool checked_end(u64 address, usz length) {
            return address <= ~u64{0} - length;
        }

        bool read_bytes(const memory_reader& reader, u64 address, u8* destination,
                        usz length) {
            if (reader.read == nullptr || destination == nullptr ||
                !checked_end(address, length))
                return false;

            return reader.read(reader.context, address, destination, length);
        }

        u8 checksum(const u8* bytes, usz length) {
            u8 sum = 0;

            for (usz i = 0; i < length; ++i) {
                sum = static_cast<u8>(sum + bytes[i]);
            }

            return sum;
        }

        bool rsdp_signature_ok(const u8* bytes) {
            return load_u64(bytes) == signature_rsdp;
        }

        bool checksum_range(const memory_reader& reader, u64 address, usz length) {
            u8  chunk[64]{};
            u8  sum    = 0;
            usz offset = 0;

            while (offset < length) {
                const usz count =
                    length - offset < sizeof(chunk) ? length - offset : sizeof(chunk);

                if (!read_bytes(reader, address + offset, chunk, count))
                    return false;

                for (usz i = 0; i < count; ++i) {
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

        bool append_table(const memory_reader& reader, u64 address, table_set& out,
                          table_error& error) {
            if (out.count == max_tables) {
                error = table_error::too_many_tables;

                return false;
            }

            u8 header[sdt_header_size]{};

            if (!read_bytes(reader, address, header, sdt_header_size)) {
                error = table_error::truncated;

                return false;
            }

            const u32 length = load_u32(header + 4);

            if (length < sdt_header_size || length > max_table_bytes) {
                error = table_error::bad_table_length;

                return false;
            }

            if (out.total_bytes > max_total_table_bytes - length ||
                out.total_bytes + length > max_total_table_bytes) {
                error = table_error::too_large;

                return false;
            }

            if (out.inventory_count == max_tables) {
                error = table_error::too_many_tables;

                return false;
            }

            if (out.allocator.allocate == nullptr || out.allocator.release == nullptr) {
                error = table_error::out_of_memory;

                return false;
            }

            void* allocation = out.allocator.allocate(out.allocator.context,
                                                      sizeof(table_record) + length);
            if (allocation == nullptr) {
                error = table_error::out_of_memory;

                return false;
            }

            auto* record = ::new (allocation) table_record{};
            static_assert(std::is_trivially_destructible_v<table_record>);

            const auto discard = [&] {
                record->~table_record();
                out.allocator.release(out.allocator.context, allocation);
            };

            record->allocation = allocation;
            record->signature  = load_u32(header);
            record->length     = length;
            record->revision   = header[8];
            record->aml        = record->signature == dsdt_signature ||
                          record->signature == ssdt_signature;

            u8* const bytes = reinterpret_cast<u8*>(record + 1);

            if (!read_bytes(reader, address, bytes, length)) {
                discard();

                error = table_error::truncated;

                return false;
            }

            if (load_u32(bytes) != record->signature ||
                load_u32(bytes + 4) != record->length) {
                discard();

                error = table_error::source_changed;

                return false;
            }

            if (checksum(bytes, length) != 0) {
                discard();

                error = table_error::bad_table_checksum;

                return false;
            }

            if (load_u32(bytes) == 0) {
                discard();

                error = table_error::bad_table_signature;

                return false;
            }

            record->value = {
                span<const u8>(bytes, length),
                address,
            };

            if (out.tail == nullptr)
                out.head = record;
            else
                out.tail->next = record;

            out.tail = record;

            ++out.count;

            out.total_bytes += length;

            out.inventory[out.inventory_count++] = {
                record->signature,
                record->length,
                record->revision,
                record->aml,
            };

            switch (record->signature) {
            case fadt_signature:
                if (out.fadt.bytes.empty())
                    out.fadt = record->value;

                break;
            case dsdt_signature:
                if (out.dsdt.bytes.empty())
                    out.dsdt = record->value;

                break;
            case ssdt_signature:
                if (out.ssdt_count < max_ssdts)
                    out.ssdt[out.ssdt_count++] = record->value;

                break;
            default:
                break;
            }

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

        for (usz i = 0; i < 8; ++i) {
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
        case table_error::out_of_memory:
            return "table allocation";
        case table_error::source_changed:
            return "table changed during read";
        case table_error::no_fadt:
            return "FADT absent";
        case table_error::no_dsdt:
            return "DSDT absent";
        }

        return "unknown";
    }

    table_set::~table_set() {
        clear();
    }

    void table_set::release_records() {
        table_record* record = head;

        while (record != nullptr) {
            table_record* next       = record->next;
            void*         allocation = record->allocation;

            if (allocator.release != nullptr && allocation != nullptr) {
                record->~table_record();
                allocator.release(allocator.context, allocation);
            }

            record = next;
        }

        head        = nullptr;
        tail        = nullptr;
        count       = 0;
        total_bytes = 0;
    }

    void table_set::clear() {
        release_records();

        allocator          = {};
        fadt               = {};
        dsdt               = {};
        ssdt_count         = 0;
        inventory_count    = 0;
        error              = table_error::none;
        rsdp_address       = 0;
        valid_rsdp         = false;
        root_address_space = address_space_kind::none;
        root_signature     = 0;

        for (table& entry : ssdt) {
            entry = {};
        }

        for (inventory_entry& entry : inventory) {
            entry = {};
        }
    }

    bool table_set::find(u32 wanted, usz occurrence, table& out) const {
        table_record* record = nullptr;

        if (!find(wanted, occurrence, record))
            return false;

        out = record->value;

        return true;
    }

    bool table_set::find(u32 wanted, usz occurrence, table_record*& out) const {
        usz index = 0;

        for (table_record* record = head; record != nullptr; record = record->next) {
            if (record->signature != wanted)
                continue;

            if (index == occurrence) {
                out = record;

                return true;
            }

            ++index;
        }

        return false;
    }

    bool discover(const memory_reader& reader, u64 rsdp_address,
                  const table_allocator& allocator, table_set& out) {
        out.clear();

        out.allocator    = allocator;
        out.rsdp_address = rsdp_address;

        const auto fail = [&](table_error error) {
            const bool valid = out.valid_rsdp;

            out.clear();

            out.allocator    = allocator;
            out.rsdp_address = rsdp_address;
            out.valid_rsdp   = valid;
            out.error        = error;

            return false;
        };

        if (rsdp_address == 0)
            return fail(table_error::no_rsdp);

        rsdp_info rsdp{};

        if (!validate_rsdp(reader, rsdp_address, rsdp)) {
            u8 header[rsdp_v1_size]{};

            if (!read_bytes(reader, rsdp_address, header, rsdp_v1_size))
                return fail(table_error::no_rsdp);

            if (!rsdp_signature_ok(header))
                return fail(table_error::bad_rsdp_signature);

            if (checksum(header, rsdp_v1_size) != 0)
                return fail(table_error::bad_rsdp_checksum);

            return fail(table_error::bad_rsdp_length);
        }

        out.valid_rsdp = true;

        const u64 root_address = rsdp.xsdt != 0 ? rsdp.xsdt : rsdp.rsdt;

        if (root_address == 0)
            return fail(table_error::bad_root_table);

        table_error error = table_error::none;

        if (!append_table(reader, root_address, out, error))
            return fail(error);

        const table root = out.head->value;

        const u32 root_signature = load_u32(root.bytes.data());
        const usz root_length    = root.bytes.size();

        if (root_signature != xsdt_signature && root_signature != rsdt_signature)
            return fail(table_error::bad_root_table);

        if (root_length < sdt_header_size)
            return fail(table_error::bad_table_length);

        const usz entry_size = root_signature == xsdt_signature ? 8 : 4;

        if ((root_length - sdt_header_size) % entry_size != 0)
            return fail(table_error::bad_table_length);

        const usz entry_count = (root_length - sdt_header_size) / entry_size;

        if (entry_count + 1 > max_tables)
            return fail(table_error::too_many_tables);

        out.root_signature     = root_signature;
        out.root_address_space = address_space_kind_for(root_signature);

        u64 table_addresses[max_tables]{};

        usz table_address_count = 1;

        table_addresses[0] = root_address;

        for (usz i = 0; i < entry_count; ++i) {
            const u8* entry = root.bytes.data() + sdt_header_size + i * entry_size;

            const u64 address = entry_size == 8 ? load_u64(entry) : load_u32(entry);

            if (address == 0)
                return fail(table_error::bad_root_table);

            for (usz k = 0; k < table_address_count; ++k) {
                if (table_addresses[k] == address)
                    return fail(table_error::duplicate_table);
            }

            if (table_address_count == max_tables)
                return fail(table_error::too_many_tables);

            table_addresses[table_address_count++] = address;

            if (!append_table(reader, address, out, error))
                return fail(error);
        }

        table fadt{};

        if (!out.find(fadt_signature, 0, fadt))
            return fail(table_error::no_fadt);

        if (out.dsdt.bytes.empty()) {
            if (fadt.bytes.size() < 44)
                return fail(table_error::bad_table_length);

            u64 dsdt_address = load_u32(fadt.bytes.data() + 40);

            if (fadt.bytes.size() >= 148) {
                const u64 extended = load_u64(fadt.bytes.data() + 140);
                if (extended != 0)
                    dsdt_address = extended;
            }

            if (dsdt_address == 0)
                return fail(table_error::no_dsdt);

            for (usz k = 0; k < table_address_count; ++k) {
                if (table_addresses[k] == dsdt_address)
                    return fail(table_error::duplicate_table);
            }

            if (table_address_count == max_tables)
                return fail(table_error::too_many_tables);

            if (!append_table(reader, dsdt_address, out, error))
                return fail(error);

            if (out.dsdt.bytes.empty())
                return fail(table_error::bad_table_signature);
        }

        out.error = table_error::none;

        return true;
    }
} // namespace astia::acpi::detail
