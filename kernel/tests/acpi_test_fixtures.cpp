#include <astia/common.hpp>

#include "acpi/tables.hpp"

namespace {
    using astia::u32;
    using astia::u64;
    using astia::u8;
    using astia::usz;

    constexpr u64 fixture_base = 0x100000;
    constexpr usz fixture_size = 4096;

    constexpr usz rsdp_offset = 0x000;
    constexpr usz rsdt_offset = 0x040;
    constexpr usz fadt_offset = 0x080;
    constexpr usz dsdt_offset = 0x100;

    u8  memory[fixture_size]{};
    u64 memory_base = 0;

    struct fixture_reader {
        const u8* bytes = nullptr;
        u64       base  = 0;
        usz       size  = 0;
    };

    fixture_reader reader_context{};

    bool text_contains(const char* text, const char* needle) {
        if (text == nullptr || needle == nullptr)
            return false;

        while (*text != '\0') {
            const char* left  = text;
            const char* right = needle;

            while (*left != '\0' && *right != '\0' && *left == *right) {
                ++left;
                ++right;
            }

            if (*right == '\0')
                return true;

            ++text;
        }

        return false;
    }

    void write_u32(u8* bytes, u32 value) {
        for (usz i = 0; i < 4; ++i) {
            bytes[i] = static_cast<u8>(value >> (i * 8));
        }
    }

    void finalize_checksum(u8* bytes, usz length) {
        bytes[9] = 0;

        u8 sum = 0;

        for (usz i = 0; i < length; ++i) {
            sum = static_cast<u8>(sum + bytes[i]);
        }

        bytes[9] = static_cast<u8>(0x100 - sum);
    }

    u8* table_at(usz offset) {
        return memory + offset;
    }

    usz write_table(usz offset, const char* signature, u8 revision, const u8* body,
                    usz body_length) {
        u8* table = table_at(offset);

        const usz length = 36 + body_length;

        for (usz i = 0; i < 4; ++i) {
            table[i] = static_cast<u8>(signature[i]);
        }

        write_u32(table + 4, static_cast<u32>(length));

        table[8]  = revision;
        table[9]  = 0;
        table[10] = 'A';
        table[11] = 'S';
        table[12] = 'T';
        table[13] = 'I';
        table[14] = 'A';

        if (body_length != 0)
            __builtin_memcpy(table + 36, body, body_length);

        finalize_checksum(table, length);

        return length;
    }

    void build_tables(bool corrupt_rsdp, bool malformed_aml) {
        for (usz i = 0; i < fixture_size; ++i) {
            memory[i] = 0;
        }

        const u64 rsdt_address = fixture_base + rsdt_offset;
        const u64 fadt_address = fixture_base + fadt_offset;
        const u64 dsdt_address = fixture_base + dsdt_offset;

        u8 rsdt_body[4]{};

        write_u32(rsdt_body, static_cast<u32>(fadt_address));

        const usz rsdt_length =
            write_table(rsdt_offset, "RSDT", 1, rsdt_body, sizeof(rsdt_body));

        u8 fadt_body[80]{};

        write_u32(fadt_body + 40 - 36, static_cast<u32>(dsdt_address));
        write_u32(fadt_body + 64 - 36, 0x604);

        fadt_body[89 - 36] = 2;

        write_table(fadt_offset, "FACP", 3, fadt_body, sizeof(fadt_body));

        const u8 good_aml[] = {
            0x08, 0x5F, 0x53, 0x35, 0x5F, 0x12, 0x06, 0x04, 0x00, 0x00, 0x00, 0x00,
        };

        const u8 bad_aml[] = {0x5B};

        const u8* aml = malformed_aml ? bad_aml : good_aml;

        const usz aml_length = malformed_aml ? sizeof(bad_aml) : sizeof(good_aml);

        write_table(dsdt_offset, "DSDT", 2, aml, aml_length);

        u8* rsdp = table_at(rsdp_offset);

        for (usz i = 0; i < 8; ++i) {
            rsdp[i] = static_cast<u8>("RSD PTR "[i]);
        }

        rsdp[15] = 0;

        write_u32(rsdp + 16, static_cast<u32>(rsdt_address));

        rsdp[8] = 0;

        u8 rsdp_sum = 0;

        for (usz i = 0; i < 20; ++i) {
            rsdp_sum = static_cast<u8>(rsdp_sum + rsdp[i]);
        }

        rsdp[8] = static_cast<u8>(0x100 - rsdp_sum);

        if (corrupt_rsdp)
            rsdp[8] = static_cast<u8>(rsdp[8] ^ 0xFF);

        (void) rsdt_length;

        reader_context.bytes = memory;
        reader_context.base  = fixture_base;
        reader_context.size  = fixture_size;
        memory_base          = fixture_base;
    }

    bool read_fixture(void* context, u64 address, u8* destination, usz length) {
        const auto* reader = static_cast<const fixture_reader*>(context);

        if (reader == nullptr || reader->bytes == nullptr || address < reader->base)
            return false;

        const u64 offset = address - reader->base;

        if (offset > reader->size || length > reader->size - offset)
            return false;

        __builtin_memcpy(destination, reader->bytes + offset, length);

        return true;
    }
} // namespace

extern "C" bool astia_acpi_test_override(const char*                         cmdline,
                                         astia::acpi::detail::memory_reader& reader,
                                         astia::u64&                         rsdp) {
    const bool corrupt_rsdp  = text_contains(cmdline, "--acpi-test=bad-rsdp");
    const bool malformed_aml = text_contains(cmdline, "--acpi-test=bad-aml");

    if (!corrupt_rsdp && !malformed_aml)
        return false;

    build_tables(corrupt_rsdp, malformed_aml);

    reader.context = &reader_context;
    reader.read    = read_fixture;
    rsdp           = memory_base + rsdp_offset;

    return true;
}
