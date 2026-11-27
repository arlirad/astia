#include <astia/acpi.hpp>
#include <astia/log.hpp>
#include <astia/mm/memory_map.hpp>
#include <astia/shutdown.hpp>

#include "acpi/aml.hpp"
#include "acpi/limits.hpp"
#include "acpi/power.hpp"
#include "acpi/tables.hpp"
#include "test.hpp"

#include <cstdlib>

namespace astia::log::detail {
    void emit(level, const char*, astia::u32) {}

    void prefix(record_builder&, level, const char*, astia::u32) {}
} // namespace astia::log::detail

namespace {
    void* host_allocate(void*, astia::usz bytes) {
        return std::malloc(bytes);
    }

    void host_release(void*, void* pointer) {
        std::free(pointer);
    }

    const astia::acpi::detail::table_allocator host_allocator{
        nullptr,
        host_allocate,
        host_release,
    };

    using astia::u16;
    using astia::u32;
    using astia::u64;
    using astia::u8;
    using astia::usz;

    constexpr u64 image_base  = 0x100000;
    constexpr usz image_size  = 64 * 1024;
    constexpr usz rsdp_offset = 0x000;
    constexpr usz root_offset = 0x040;
    constexpr usz fadt_offset = 0x100;
    constexpr usz dsdt_offset = 0x200;

    struct image {
        u8  bytes[image_size]{};
        usz used = 0;
    };

    void store_u32(u8* bytes, u32 value) {
        for (usz i = 0; i < 4; ++i) {
            bytes[i] = static_cast<u8>(value >> (i * 8));
        }
    }

    void store_u64(u8* bytes, u64 value) {
        for (usz i = 0; i < 8; ++i) {
            bytes[i] = static_cast<u8>(value >> (i * 8));
        }
    }

    u8 sum(const u8* bytes, usz length) {
        u8 value = 0;

        for (usz i = 0; i < length; ++i) {
            value = static_cast<u8>(value + bytes[i]);
        }

        return value;
    }

    void checksum_table(u8* table, usz length) {
        table[9] = 0;
        table[9] = static_cast<u8>(0x100 - sum(table, length));
    }

    usz add_table(image& target, usz offset, const char (&signature)[5], u8 revision,
                  const u8* body, usz body_length) {
        u8* const table = target.bytes + offset;

        const usz length = 36 + body_length;

        for (usz i = 0; i < 4; ++i) {
            table[i] = static_cast<u8>(signature[i]);
        }

        store_u32(table + 4, static_cast<u32>(length));

        table[8]  = revision;
        table[9]  = 0;
        table[10] = 'A';
        table[11] = 'S';
        table[12] = 'T';
        table[13] = 'I';
        table[14] = 'A';

        for (usz i = 0; i < body_length; ++i) {
            table[36 + i] = body != nullptr ? body[i] : 0;
        }

        checksum_table(table, length);

        if (offset + length > target.used)
            target.used = offset + length;

        return length;
    }

    struct raw_reader {
        const image* target = nullptr;
        u64          base   = 0;
    };

    bool read_image(void* context, u64 address, u8* destination, usz length) {
        const auto* reader = static_cast<const raw_reader*>(context);

        if (reader == nullptr || reader->target == nullptr || address < reader->base)
            return false;

        const u64 offset = address - reader->base;

        if (offset > image_size || length > image_size - offset)
            return false;

        __builtin_memcpy(destination, reader->target->bytes + offset, length);

        return true;
    }

    astia::acpi::detail::memory_reader reader_for(raw_reader& context) {
        return {
            &context,
            read_image,
        };
    }

    void write_rsdp(u8* rsdp, u64 rsdt, u64 xsdt, bool extended) {
        for (usz i = 0; i < 8; ++i) {
            rsdp[i] = static_cast<u8>("RSD PTR "[i]);
        }

        rsdp[8]  = 0;
        rsdp[15] = extended ? 2 : 0;

        store_u32(rsdp + 16, static_cast<u32>(rsdt));

        rsdp[8] = static_cast<u8>(0x100 - sum(rsdp, 20));

        if (extended) {
            store_u32(rsdp + 20, 36);
            store_u64(rsdp + 24, xsdt);

            rsdp[32] = 0;
            rsdp[32] = static_cast<u8>(0x100 - sum(rsdp, 36));
        }
    }

    struct table_image {
        image                            data{};
        astia::acpi::detail::table_set   tables{};
        astia::acpi::detail::interpreter interpreter{};
    };

    void build_standard_tables(table_image& target, bool extended_rsdp,
                               bool include_dsdt_in_root, const u8* aml, usz aml_length) {
        const u64 root_address = image_base + root_offset;
        const u64 fadt_address = image_base + fadt_offset;
        const u64 dsdt_address = image_base + dsdt_offset;

        const usz dsdt_length =
            add_table(target.data, dsdt_offset, "DSDT", 2, aml, aml_length);

        u8 fadt_body[80]{};

        store_u32(fadt_body + 4, static_cast<u32>(dsdt_address));
        store_u32(fadt_body + 28, 0x604);

        fadt_body[53] = 2;

        add_table(target.data, fadt_offset, "FACP", 3, fadt_body, sizeof(fadt_body));

        if (include_dsdt_in_root) {
            u8 root_body[8]{};

            store_u32(root_body + 0, static_cast<u32>(fadt_address));
            store_u32(root_body + 4, static_cast<u32>(dsdt_address));

            if (extended_rsdp) {
                const usz length =
                    add_table(target.data, root_offset, "XSDT", 1, nullptr, 0);

                u8* root = target.data.bytes + root_offset;

                store_u64(root + 36, fadt_address);
                store_u64(root + 44, dsdt_address);
                store_u32(root + 4, static_cast<u32>(length + 16));

                target.data.used = root_offset + length + 16;

                checksum_table(root, length + 16);
            }
            else {
                add_table(target.data, root_offset, "RSDT", 1, root_body,
                          sizeof(root_body));
            }
        }
        else {
            if (extended_rsdp)
                add_table(target.data, root_offset, "XSDT", 1, nullptr, 0);
            else
                add_table(target.data, root_offset, "RSDT", 1, nullptr, 0);

            u8* root = target.data.bytes + root_offset;

            const usz length = extended_rsdp ? 44 : 40;

            store_u32(root + 4, static_cast<u32>(length));

            if (extended_rsdp)
                store_u64(root + 36, fadt_address);
            else
                store_u32(root + 36, static_cast<u32>(fadt_address));

            target.data.used = root_offset + length;

            checksum_table(root, length);
        }

        write_rsdp(target.data.bytes + rsdp_offset, root_address,
                   extended_rsdp ? root_address : 0, extended_rsdp);

        (void) dsdt_length;
    }

    raw_reader context_for(const image& source) {
        return {
            &source,
            image_base,
        };
    }

    constexpr u8 name_s5_package[] = {
        0x08, 0x5F, 0x53, 0x35, 0x5F, 0x12, 0x06, 0x04, 0x00, 0x00, 0x00, 0x00,
    };

    constexpr u8 name_s5_nonzero_package[] = {
        0x08, 0x5F, 0x53, 0x35, 0x5F, 0x12, 0x05, 0x02, 0x0A, 0x05, 0x00,
    };

    constexpr u8 name_s5_invalid[] = {
        0x08, 0x5F, 0x53, 0x35, 0x5F, 0x12, 0x07, 0x04, 0x0A, 0x08, 0x00, 0x00, 0x00,
    };

    constexpr u8 method_s5_package[] = {
        0x14, 0x0E, 0x5F, 0x53, 0x35, 0x5F, 0x00, 0xA4,
        0x12, 0x06, 0x04, 0x00, 0x00, 0x00, 0x00,
    };

    constexpr u8 alias_sibling_aml[] = {
        0x08, 0x54, 0x41, 0x52, 0x47, 0x12, 0x06, 0x04, 0x00, 0x00, 0x00, 0x00, 0x08,
        0x53, 0x49, 0x42, 0x4C, 0x12, 0x06, 0x04, 0x00, 0x00, 0x00, 0x00, 0x06, 0x54,
        0x41, 0x52, 0x47, 0x41, 0x4C, 0x49, 0x41, 0x14, 0x11, 0x5F, 0x53, 0x35, 0x5F,
        0x00, 0x70, 0x41, 0x4C, 0x49, 0x41, 0x60, 0xA4, 0x53, 0x49, 0x42, 0x4C,
    };

    constexpr u8 s5_alias_aml[] = {
        0x08, 0x53, 0x35, 0x50, 0x4B, 0x12, 0x06, 0x04, 0x00, 0x00, 0x00,
        0x00, 0x06, 0x53, 0x35, 0x50, 0x4B, 0x5F, 0x53, 0x35, 0x5F,
    };

    constexpr u8 s5_alias_scope_reopen_aml[] = {
        0x10, 0x11, 0x53, 0x43, 0x4F, 0x50, 0x08, 0x53, 0x35, 0x50, 0x4B, 0x12, 0x06,
        0x04, 0x00, 0x00, 0x00, 0x00, 0x10, 0x11, 0x53, 0x43, 0x4F, 0x50, 0x08, 0x53,
        0x49, 0x42, 0x4C, 0x12, 0x06, 0x04, 0x00, 0x00, 0x00, 0x00, 0x06, 0x5C, 0x2E,
        0x53, 0x43, 0x4F, 0x50, 0x53, 0x35, 0x50, 0x4B, 0x5F, 0x53, 0x35, 0x5F,
    };

    constexpr u8 truncated_if_package_aml[] = {
        0x14, 0x08, 0x5F, 0x53, 0x35, 0x5F, 0x00, 0xA0, 0x7F,
    };

    constexpr u8 truncated_aml[] = {0x5B};

    constexpr u8 unsupported_aml[] = {0x5B, 0x2A};

    void build_fadt_with_dsdt(u64 dsdt_address, u8* fadt, usz length) {
        store_u32(fadt + 40, static_cast<u32>(dsdt_address));
        store_u32(fadt + 64, 0x604);

        fadt[89] = 2;

        checksum_table(fadt, length);
    }

    void build_duplicate_signature_tables(table_image& target) {
        const u64 root_address  = image_base + root_offset;
        const u64 fadt_address  = image_base + fadt_offset;
        const u64 dsdt_address  = image_base + dsdt_offset;
        const u64 ssdt1_address = image_base + 0x300;
        const u64 ssdt2_address = image_base + 0x400;

        add_table(target.data, dsdt_offset, "DSDT", 2, name_s5_package,
                  sizeof(name_s5_package));

        u8 fadt_body[80]{};

        store_u32(fadt_body + 4, static_cast<u32>(dsdt_address));
        store_u32(fadt_body + 28, 0x604);

        fadt_body[53] = 2;

        add_table(target.data, fadt_offset, "FACP", 3, fadt_body, sizeof(fadt_body));

        const u8 ssdt_body[] = {0x08, 0x58, 0x58, 0x58, 0x58, 0x00};

        add_table(target.data, 0x300, "SSDT", 2, ssdt_body, sizeof(ssdt_body));
        add_table(target.data, 0x400, "SSDT", 2, ssdt_body, sizeof(ssdt_body));

        u8 root_body[16]{};

        store_u32(root_body + 0, static_cast<u32>(fadt_address));
        store_u32(root_body + 4, static_cast<u32>(dsdt_address));
        store_u32(root_body + 8, static_cast<u32>(ssdt1_address));
        store_u32(root_body + 12, static_cast<u32>(ssdt2_address));

        add_table(target.data, root_offset, "RSDT", 1, root_body, sizeof(root_body));

        write_rsdp(target.data.bytes + rsdp_offset, root_address, 0, false);
    }
} // namespace

ASTIA_TEST(acpi_checked_add_rejects_overflow) {
    u64 result = 0;

    ASTIA_CHECK(astia::acpi::detail::checked_add(0, 0, result) && result == 0);
    ASTIA_CHECK(astia::acpi::detail::checked_add(0x1000, 0x2000, result) &&
                result == 0x3000);
    ASTIA_CHECK(astia::acpi::detail::checked_add(~u64{0} - 1, 1, result) &&
                result == ~u64{0});
    ASTIA_CHECK(!astia::acpi::detail::checked_add(~u64{0}, 1, result));
    ASTIA_CHECK(!astia::acpi::detail::checked_add(1, ~u64{0}, result));
    ASTIA_CHECK(!astia::acpi::detail::checked_add(~u64{0}, ~u64{0}, result));
}

ASTIA_TEST(acpi_checked_align_up_rejects_overflow) {
    constexpr u64 two_m = 2ull << 20;

    u64 result = 0;

    ASTIA_CHECK(astia::acpi::detail::checked_align_up(0, two_m, result) && result == 0);
    ASTIA_CHECK(astia::acpi::detail::checked_align_up(1, two_m, result) &&
                result == two_m);
    ASTIA_CHECK(astia::acpi::detail::checked_align_up(two_m, two_m, result) &&
                result == two_m);
    ASTIA_CHECK(astia::acpi::detail::checked_align_up(two_m + 1, two_m, result) &&
                result == two_m * 2);
    ASTIA_CHECK(
        astia::acpi::detail::checked_align_up(~u64{0} - (two_m - 1), two_m, result) &&
        result == ~u64{0} - (two_m - 1));
    ASTIA_CHECK(
        !astia::acpi::detail::checked_align_up(~u64{0} - (two_m - 2), two_m, result));
    ASTIA_CHECK(!astia::acpi::detail::checked_align_up(~u64{0}, two_m, result));
    ASTIA_CHECK(!astia::acpi::detail::checked_align_up(0, 0, result));
    ASTIA_CHECK(!astia::acpi::detail::checked_align_up(0, 3, result));
}

ASTIA_TEST(acpi_usable_top_ignores_non_usable_regions) {
    astia::mm::memory_map map;

    map.add(0x1000, 0x1000, astia::mm::memory_type::usable);
    map.add(0x5000, 0x1000, astia::mm::memory_type::reserved);
    map.add(0x2000, 0x1000, astia::mm::memory_type::usable);
    map.add(~u64{0}, 1, astia::mm::memory_type::reserved);

    u64 top = 0;

    ASTIA_CHECK(astia::acpi::detail::usable_top(map, top));
    ASTIA_CHECK(top == 0x3000);
}

ASTIA_TEST(acpi_usable_top_rejects_overflowing_region) {
    astia::mm::memory_map map;

    map.add(0x1000, 0x1000, astia::mm::memory_type::usable);
    map.add(~u64{0} - 0x10, 0x20, astia::mm::memory_type::usable);

    u64 top = 0;

    ASTIA_CHECK(!astia::acpi::detail::usable_top(map, top));
}

ASTIA_TEST(acpi_direct_map_limit_uses_aligned_usable_top) {
    astia::mm::memory_map map;

    map.add(0x1000, 0x1000, astia::mm::memory_type::usable);

    u64 limit = 0;

    ASTIA_CHECK(astia::acpi::detail::direct_map_limit(map, limit));
    ASTIA_CHECK(limit == astia::acpi::detail::direct_map_alignment);

    map.add(0x2000, 0x1000, astia::mm::memory_type::usable);

    ASTIA_CHECK(astia::acpi::detail::direct_map_limit(map, limit));
    ASTIA_CHECK(limit == astia::acpi::detail::direct_map_alignment);
}

ASTIA_TEST(acpi_direct_map_limit_rejects_wrapping_top) {
    astia::mm::memory_map map;

    map.add(~u64{0} - 0xf, 0xf, astia::mm::memory_type::usable);

    u64 top   = 0;
    u64 limit = 0;

    ASTIA_CHECK(astia::acpi::detail::usable_top(map, top));
    ASTIA_CHECK(top == ~u64{0});
    ASTIA_CHECK(!astia::acpi::detail::direct_map_limit(map, limit));
}

ASTIA_TEST(acpi_rsdp_v1_and_rsdt_validate) {
    table_image target;

    build_standard_tables(target, false, true, name_s5_package, sizeof(name_s5_package));

    raw_reader context = context_for(target.data);

    ASTIA_CHECK(astia::acpi::detail::discover(
        reader_for(context), image_base + rsdp_offset, host_allocator, target.tables));
    ASTIA_CHECK(target.tables.valid_rsdp);
    ASTIA_CHECK(target.tables.dsdt.bytes.size() == sizeof(name_s5_package) + 36);
    ASTIA_CHECK(target.tables.fadt.bytes.size() == 116);
}

ASTIA_TEST(acpi_rsdp_v2_and_xsdt_validate) {
    table_image target;

    build_standard_tables(target, true, true, name_s5_package, sizeof(name_s5_package));

    raw_reader context = context_for(target.data);

    ASTIA_CHECK(astia::acpi::detail::discover(
        reader_for(context), image_base + rsdp_offset, host_allocator, target.tables));
    ASTIA_CHECK(target.tables.valid_rsdp);
    ASTIA_CHECK(target.tables.root_signature == astia::acpi::detail::xsdt_signature);
}

ASTIA_TEST(acpi_rsdp_bad_checksum_rejects) {
    table_image target;

    build_standard_tables(target, false, true, name_s5_package, sizeof(name_s5_package));

    target.data.bytes[rsdp_offset + 8] ^= 0xFF;

    raw_reader context = context_for(target.data);

    ASTIA_CHECK(!astia::acpi::detail::discover(
        reader_for(context), image_base + rsdp_offset, host_allocator, target.tables));
    ASTIA_CHECK(target.tables.error ==
                astia::acpi::detail::table_error::bad_rsdp_checksum);
    ASTIA_CHECK(!target.tables.valid_rsdp);
}

ASTIA_TEST(acpi_table_bad_checksum_rejects) {
    table_image target;

    build_standard_tables(target, false, true, name_s5_package, sizeof(name_s5_package));

    target.data.bytes[dsdt_offset + 9] ^= 0xFF;

    raw_reader context = context_for(target.data);

    ASTIA_CHECK(!astia::acpi::detail::discover(
        reader_for(context), image_base + rsdp_offset, host_allocator, target.tables));
    ASTIA_CHECK(target.tables.error ==
                astia::acpi::detail::table_error::bad_table_checksum);
}

ASTIA_TEST(acpi_table_duplicate_rejects) {
    table_image target;

    build_standard_tables(target, false, true, name_s5_package, sizeof(name_s5_package));

    u8* root = target.data.bytes + root_offset;

    store_u32(root + 40, static_cast<u32>(image_base + fadt_offset));
    checksum_table(root, 44);

    raw_reader context = context_for(target.data);

    ASTIA_CHECK(!astia::acpi::detail::discover(
        reader_for(context), image_base + rsdp_offset, host_allocator, target.tables));
    ASTIA_CHECK(target.tables.error == astia::acpi::detail::table_error::duplicate_table);
}

ASTIA_TEST(acpi_table_store_finds_duplicate_signatures_in_order) {
    table_image target;

    build_duplicate_signature_tables(target);

    raw_reader context = context_for(target.data);

    ASTIA_CHECK(astia::acpi::detail::discover(
        reader_for(context), image_base + rsdp_offset, host_allocator, target.tables));
    ASTIA_CHECK(target.tables.count == 5);

    astia::acpi::detail::table first{};
    astia::acpi::detail::table second{};
    astia::acpi::detail::table missing{};

    ASTIA_CHECK(target.tables.find(astia::acpi::detail::ssdt_signature, 0, first));
    ASTIA_CHECK(target.tables.find(astia::acpi::detail::ssdt_signature, 1, second));
    ASTIA_CHECK(!target.tables.find(astia::acpi::detail::ssdt_signature, 2, missing));
    ASTIA_CHECK(first.address == image_base + 0x300);
    ASTIA_CHECK(second.address == image_base + 0x400);
    ASTIA_CHECK(first.bytes.size() == 42);
    ASTIA_CHECK(second.bytes.size() == 42);
}

ASTIA_TEST(acpi_table_store_owns_bytes_after_source_changes) {
    table_image target;

    build_standard_tables(target, false, true, name_s5_package, sizeof(name_s5_package));

    raw_reader context = context_for(target.data);

    ASTIA_CHECK(astia::acpi::detail::discover(
        reader_for(context), image_base + rsdp_offset, host_allocator, target.tables));

    astia::acpi::detail::table stored{};

    ASTIA_CHECK(target.tables.find(astia::acpi::detail::dsdt_signature, 0, stored));

    const u8 first_byte = stored.bytes[0];

    target.data.bytes[dsdt_offset] = static_cast<u8>(first_byte ^ 0xFF);

    astia::acpi::detail::table again{};

    ASTIA_CHECK(target.tables.find(astia::acpi::detail::dsdt_signature, 0, again));
    ASTIA_CHECK(again.bytes[0] == first_byte);
}

namespace {
    struct failing_allocator {
        usz calls   = 0;
        usz fail_at = 0;
    };

    void* fail_allocate(void* context, usz bytes) {
        auto* state = static_cast<failing_allocator*>(context);

        ++state->calls;

        if (state->calls == state->fail_at)
            return nullptr;

        return std::malloc(bytes);
    }
} // namespace

namespace {
    struct mutating_reader {
        image* target           = nullptr;
        bool   mutate_signature = false;
        int    header_reads     = 0;
    };

    bool read_mutating(void* context, u64 address, u8* destination, usz length) {
        auto* reader = static_cast<mutating_reader*>(context);

        if (reader == nullptr || reader->target == nullptr || address < image_base)
            return false;

        const u64 offset = address - image_base;

        if (offset > image_size || length > image_size - offset)
            return false;

        __builtin_memcpy(destination, reader->target->bytes + offset, length);

        if (address == image_base + root_offset && length == 36 &&
            reader->header_reads++ == 0) {
            if (reader->mutate_signature)
                reader->target->bytes[root_offset] = 'X';
            else
                store_u32(reader->target->bytes + root_offset + 4, 32);
        }

        return true;
    }
} // namespace

ASTIA_TEST(acpi_table_store_rejects_source_header_changes) {
    for (int mutation = 0; mutation < 2; ++mutation) {
        table_image target;

        build_standard_tables(target, false, true, name_s5_package,
                              sizeof(name_s5_package));

        mutating_reader source{
            &target.data,
            mutation != 0,
            0,
        };

        ASTIA_CHECK(!astia::acpi::detail::discover({&source, read_mutating},
                                                   image_base + rsdp_offset,
                                                   host_allocator, target.tables));
        ASTIA_CHECK(target.tables.error ==
                    astia::acpi::detail::table_error::source_changed);
        ASTIA_CHECK(target.tables.count == 0);
        ASTIA_CHECK(target.tables.total_bytes == 0);
    }
}

ASTIA_TEST(acpi_table_store_allocation_failure_cleans_up) {
    table_image target;

    build_standard_tables(target, false, true, name_s5_package, sizeof(name_s5_package));

    raw_reader        context = context_for(target.data);
    failing_allocator state{0, 2};

    ASTIA_CHECK(!astia::acpi::detail::discover(
        reader_for(context), image_base + rsdp_offset,
        {&state, fail_allocate, host_release}, target.tables));
    ASTIA_CHECK(target.tables.error == astia::acpi::detail::table_error::out_of_memory);
    ASTIA_CHECK(target.tables.count == 0);
    ASTIA_CHECK(target.tables.total_bytes == 0);

    astia::acpi::detail::table missing{};

    ASTIA_CHECK(!target.tables.find(astia::acpi::detail::dsdt_signature, 0, missing));
}

ASTIA_TEST(acpi_aml_finds_s5_name_package) {
    table_image target;

    build_standard_tables(target, false, true, name_s5_package, sizeof(name_s5_package));

    target.tables.dsdt = {
        astia::span<const u8>(target.data.bytes + dsdt_offset,
                              sizeof(name_s5_package) + 36),
        image_base + dsdt_offset,
    };

    ASTIA_CHECK(target.interpreter.build(target.tables));

    astia::acpi::detail::value_id package = astia::acpi::detail::no_value;

    ASTIA_CHECK(target.interpreter.evaluate_s5(package));

    const auto* value = target.interpreter.get(package);

    ASTIA_CHECK(value != nullptr);
    ASTIA_CHECK(value->count == 4);

    astia::acpi::detail::value_id item = value->first;

    u64 number = 0;

    ASTIA_CHECK(target.interpreter.integer(item, number));
    ASTIA_CHECK(number == 0);
}

ASTIA_TEST(acpi_aml_finds_s5_method_package) {
    table_image target;

    build_standard_tables(target, false, true, method_s5_package,
                          sizeof(method_s5_package));

    target.tables.dsdt = {
        astia::span<const u8>(target.data.bytes + dsdt_offset,
                              sizeof(method_s5_package) + 36),
        image_base + dsdt_offset,
    };

    ASTIA_CHECK(target.interpreter.build(target.tables));

    astia::acpi::detail::value_id package = astia::acpi::detail::no_value;

    ASTIA_CHECK(target.interpreter.evaluate_s5(package));
    ASTIA_CHECK(target.interpreter.get(package) != nullptr);
    ASTIA_CHECK(target.interpreter.get(package)->count == 4);
}

ASTIA_TEST(acpi_aml_rejects_truncated_input) {
    table_image target;

    build_standard_tables(target, false, true, truncated_aml, sizeof(truncated_aml));

    target.tables.dsdt = {
        astia::span<const u8>(target.data.bytes + dsdt_offset,
                              sizeof(truncated_aml) + 36),
        image_base + dsdt_offset,
    };

    ASTIA_CHECK(!target.interpreter.build(target.tables));
    ASTIA_CHECK(target.interpreter.error() == astia::acpi::detail::aml_error::truncated);
}

ASTIA_TEST(acpi_aml_rejects_truncated_if_package) {
    table_image target;

    build_standard_tables(target, false, true, truncated_if_package_aml,
                          sizeof(truncated_if_package_aml));

    target.tables.dsdt = {
        astia::span<const u8>(target.data.bytes + dsdt_offset,
                              sizeof(truncated_if_package_aml) + 36),
        image_base + dsdt_offset,
    };

    ASTIA_CHECK(target.interpreter.build(target.tables));

    astia::acpi::detail::value_id package = astia::acpi::detail::no_value;

    ASTIA_CHECK(!target.interpreter.evaluate_s5(package));
    ASTIA_CHECK(target.interpreter.error() == astia::acpi::detail::aml_error::truncated);
}

ASTIA_TEST(acpi_aml_rejects_unsupported_opcode) {
    table_image target;

    build_standard_tables(target, false, true, unsupported_aml, sizeof(unsupported_aml));

    target.tables.dsdt = {
        astia::span<const u8>(target.data.bytes + dsdt_offset,
                              sizeof(unsupported_aml) + 36),
        image_base + dsdt_offset,
    };

    ASTIA_CHECK(!target.interpreter.build(target.tables));
    ASTIA_CHECK(target.interpreter.error() ==
                astia::acpi::detail::aml_error::unsupported_opcode);
}

ASTIA_TEST(acpi_aml_s5_alias_resolves) {
    table_image target;

    build_standard_tables(target, false, true, s5_alias_aml, sizeof(s5_alias_aml));

    target.tables.dsdt = {
        astia::span<const u8>(target.data.bytes + dsdt_offset, sizeof(s5_alias_aml) + 36),
        image_base + dsdt_offset,
    };

    ASTIA_CHECK(target.interpreter.build(target.tables));

    astia::acpi::detail::value_id package = astia::acpi::detail::no_value;

    ASTIA_CHECK(target.interpreter.evaluate_s5(package));
    ASTIA_CHECK(target.interpreter.get(package) != nullptr);
    ASTIA_CHECK(target.interpreter.get(package)->count == 4);
}

ASTIA_TEST(acpi_aml_s5_alias_scope_reopen_resolves) {
    table_image target;

    build_standard_tables(target, false, true, s5_alias_scope_reopen_aml,
                          sizeof(s5_alias_scope_reopen_aml));

    target.tables.dsdt = {
        astia::span<const u8>(target.data.bytes + dsdt_offset,
                              sizeof(s5_alias_scope_reopen_aml) + 36),
        image_base + dsdt_offset,
    };

    ASTIA_CHECK(target.interpreter.build(target.tables));

    astia::acpi::detail::value_id package = astia::acpi::detail::no_value;

    ASTIA_CHECK(target.interpreter.evaluate_s5(package));
    ASTIA_CHECK(target.interpreter.get(package) != nullptr);
    ASTIA_CHECK(target.interpreter.get(package)->count == 4);
}

ASTIA_TEST(acpi_aml_alias_does_not_mutate_namespace) {
    table_image target;

    build_standard_tables(target, false, true, alias_sibling_aml,
                          sizeof(alias_sibling_aml));

    target.tables.dsdt = {
        astia::span<const u8>(target.data.bytes + dsdt_offset,
                              sizeof(alias_sibling_aml) + 36),
        image_base + dsdt_offset,
    };

    ASTIA_CHECK(target.interpreter.build(target.tables));

    astia::acpi::detail::value_id package = astia::acpi::detail::no_value;

    ASTIA_CHECK(target.interpreter.evaluate_s5(package));
    ASTIA_CHECK(target.interpreter.get(package) != nullptr);
    ASTIA_CHECK(target.interpreter.get(package)->count == 4);

    package = astia::acpi::detail::no_value;

    ASTIA_CHECK(target.interpreter.evaluate_s5(package));
    ASTIA_CHECK(target.interpreter.get(package) != nullptr);
}

ASTIA_TEST(acpi_aml_rejects_deep_expression_recursion) {
    u8            aml[256]{};
    usz           length     = 0;
    constexpr usz lnot_count = 80;

    const usz body_length   = 1 + lnot_count + 1;
    const usz method_total  = 1 + 2 + 4 + 1 + body_length;
    const usz package_value = method_total - 1;

    aml[length++] = 0x14;
    aml[length++] = static_cast<u8>(0x40 | (package_value & 0x0F));
    aml[length++] = static_cast<u8>(package_value >> 4);
    aml[length++] = 0x5F;
    aml[length++] = 0x53;
    aml[length++] = 0x35;
    aml[length++] = 0x5F;
    aml[length++] = 0x00;
    aml[length++] = 0xA4;

    for (usz i = 0; i < lnot_count; ++i) {
        aml[length++] = 0x92;
    }

    aml[length++] = 0x00;

    table_image target;

    build_standard_tables(target, false, true, aml, length);

    target.tables.dsdt = {
        astia::span<const u8>(target.data.bytes + dsdt_offset, length + 36),
        image_base + dsdt_offset,
    };

    ASTIA_CHECK(target.interpreter.build(target.tables));

    astia::acpi::detail::value_id package = astia::acpi::detail::no_value;

    ASTIA_CHECK(!target.interpreter.evaluate_s5(package));
    ASTIA_CHECK(target.interpreter.error() ==
                astia::acpi::detail::aml_error::recursion_limit);
}

namespace {
    struct write_log {
        u16 io_port[8]{};
        u16 io_value[8]{};
        usz io_count       = 0;
        u64 memory_address = 0;
        u16 memory_value   = 0;
        usz memory_count   = 0;

        u16  io_read_port        = 0;
        u16  io_read_value       = 0;
        usz  io_read_count       = 0;
        u64  memory_read_address = 0;
        u16  memory_read_value   = 0;
        usz  memory_read_count   = 0;
        bool io_read_ok          = true;
        bool io_write_ok         = true;
        bool memory_read_ok      = true;
        bool memory_write_ok     = true;
    };

    bool read_io(void* context, u16 port, u16& value) {
        auto* log = static_cast<write_log*>(context);

        log->io_read_port = port;

        ++log->io_read_count;

        if (!log->io_read_ok)
            return false;

        value = log->io_read_value;

        return true;
    }

    bool record_io(void* context, u16 port, u16 value) {
        auto* log = static_cast<write_log*>(context);

        if (log->io_count < 8) {
            log->io_port[log->io_count]  = port;
            log->io_value[log->io_count] = value;
        }

        ++log->io_count;

        return log->io_write_ok;
    }

    bool read_memory(void* context, u64 address, u16& value) {
        auto* log = static_cast<write_log*>(context);

        log->memory_read_address = address;

        ++log->memory_read_count;

        if (!log->memory_read_ok)
            return false;

        value = log->memory_read_value;

        return true;
    }

    bool record_memory(void* context, u64 address, u16 value) {
        auto* log = static_cast<write_log*>(context);

        log->memory_address = address;
        log->memory_value   = value;

        ++log->memory_count;

        return log->memory_write_ok;
    }

    void make_fadt(table_image& target, bool extended, const u8* aml = name_s5_package,
                   usz aml_length = sizeof(name_s5_package)) {
        const usz length       = extended ? 244 : 116;
        const u64 dsdt_address = image_base + dsdt_offset;

        add_table(target.data, fadt_offset, "FACP", 3, nullptr, length - 36);

        u8* fadt = target.data.bytes + fadt_offset;

        build_fadt_with_dsdt(dsdt_address, fadt, length);

        if (extended) {
            fadt[172] = 1;
            fadt[173] = 16;
            fadt[174] = 0;
            fadt[175] = 2;

            store_u64(fadt + 176, 0x1234);
            checksum_table(fadt, length);
        }

        target.tables.fadt = {
            astia::span<const u8>(fadt, length),
            image_base + fadt_offset,
        };
        target.tables.dsdt = {
            astia::span<const u8>(target.data.bytes + dsdt_offset, aml_length + 36),
            dsdt_address,
        };

        __builtin_memcpy(target.data.bytes + dsdt_offset + 36, aml, aml_length);
    }

    void make_reset_fadt(table_image& target, u8 space, u64 address, u8 value,
                         bool supported = true) {
        make_fadt(target, true);

        u8* fadt = target.data.bytes + fadt_offset;

        store_u32(fadt + 112, supported ? 1u << 10 : 0);

        fadt[116] = space;
        fadt[117] = 8;
        fadt[118] = 0;
        fadt[119] = 1;

        store_u64(fadt + 120, address);

        fadt[128] = value;

        checksum_table(fadt, 244);

        target.tables.fadt = {
            astia::span<const u8>(fadt, 244),
            image_base + fadt_offset,
        };
    }

    struct reset_log {
        u16 io_port        = 0;
        u8  io_value       = 0;
        usz io_count       = 0;
        u64 memory_address = 0;
        u8  memory_value   = 0;
        usz memory_count   = 0;
    };

    bool record_io8(void* context, u16 port, u8 value) {
        auto* log = static_cast<reset_log*>(context);

        log->io_port  = port;
        log->io_value = value;

        ++log->io_count;

        return true;
    }

    bool record_memory8(void* context, u64 address, u8 value) {
        auto* log = static_cast<reset_log*>(context);

        log->memory_address = address;
        log->memory_value   = value;

        ++log->memory_count;

        return true;
    }
} // namespace

ASTIA_TEST(acpi_power_legacy_pm1a_plan_writes_s5) {
    table_image target;

    make_fadt(target, false);

    ASTIA_CHECK(target.interpreter.build(target.tables));

    astia::acpi::detail::s5_plan     plan{};
    astia::acpi::detail::power_error error = astia::acpi::detail::power_error::none;

    ASTIA_CHECK(astia::acpi::detail::build_s5_plan(target.tables, target.interpreter,
                                                   plan, error));
    ASTIA_CHECK(!plan.extended);
    ASTIA_CHECK(plan.pm1a.gas.space == astia::acpi::detail::gas_system_io);
    ASTIA_CHECK(plan.pm1a.gas.address == 0x604);

    write_log log{};

    ASTIA_CHECK(astia::acpi::detail::execute_s5(
        plan, {&log, read_io, record_io, read_memory, record_memory}, error));
    ASTIA_CHECK(log.io_count == 1);
    ASTIA_CHECK(log.io_port[0] == 0x604);
    ASTIA_CHECK(log.io_value[0] == 0x2000);
}

ASTIA_TEST(acpi_power_extended_gas_plan_writes_s5) {
    table_image target;

    make_fadt(target, true);

    ASTIA_CHECK(target.interpreter.build(target.tables));

    astia::acpi::detail::s5_plan     plan{};
    astia::acpi::detail::power_error error = astia::acpi::detail::power_error::none;

    ASTIA_CHECK(astia::acpi::detail::build_s5_plan(target.tables, target.interpreter,
                                                   plan, error));
    ASTIA_CHECK(plan.extended);
    ASTIA_CHECK(plan.pm1a.gas.address == 0x1234);

    write_log log{};

    ASTIA_CHECK(astia::acpi::detail::execute_s5(
        plan, {&log, read_io, record_io, read_memory, record_memory}, error));
    ASTIA_CHECK(log.io_count == 1);
    ASTIA_CHECK(log.io_port[0] == 0x1234);
    ASTIA_CHECK(log.io_value[0] == 0x2000);
}

ASTIA_TEST(acpi_power_mmio_gas_writes_memory) {
    table_image target;

    make_fadt(target, true);

    u8* fadt = target.data.bytes + fadt_offset;

    fadt[172] = 0;

    store_u64(fadt + 176, 0x1000);
    checksum_table(fadt, 244);

    ASTIA_CHECK(target.interpreter.build(target.tables));

    astia::acpi::detail::s5_plan     plan{};
    astia::acpi::detail::power_error error = astia::acpi::detail::power_error::none;

    ASTIA_CHECK(astia::acpi::detail::build_s5_plan(target.tables, target.interpreter,
                                                   plan, error));

    write_log log{};

    ASTIA_CHECK(astia::acpi::detail::execute_s5(
        plan, {&log, read_io, record_io, read_memory, record_memory}, error));
    ASTIA_CHECK(log.memory_count == 1);
    ASTIA_CHECK(log.memory_address == 0x1000);
    ASTIA_CHECK(log.memory_value == 0x2000);
    ASTIA_CHECK(log.io_count == 0);
}

ASTIA_TEST(acpi_power_pm1_control_rmw_preserves_non_sleep_bits_io) {
    table_image target;

    make_fadt(target, false, name_s5_nonzero_package, sizeof(name_s5_nonzero_package));

    ASTIA_CHECK(target.interpreter.build(target.tables));

    astia::acpi::detail::s5_plan     plan{};
    astia::acpi::detail::power_error error = astia::acpi::detail::power_error::none;

    ASTIA_CHECK(astia::acpi::detail::build_s5_plan(target.tables, target.interpreter,
                                                   plan, error));
    ASTIA_CHECK(plan.slp_typa == 5);

    write_log log{};

    log.io_read_value = 0x3E07;

    ASTIA_CHECK(astia::acpi::detail::execute_s5(
        plan, {&log, read_io, record_io, read_memory, record_memory}, error));
    ASTIA_CHECK(log.io_read_count == 1);
    ASTIA_CHECK(log.io_read_port == 0x604);
    ASTIA_CHECK(log.io_count == 1);
    ASTIA_CHECK(log.io_port[0] == 0x604);
    ASTIA_CHECK(log.io_value[0] == 0x3607);
}

ASTIA_TEST(acpi_power_pm1_control_rmw_preserves_non_sleep_bits_memory) {
    table_image target;

    make_fadt(target, true, name_s5_nonzero_package, sizeof(name_s5_nonzero_package));

    u8* fadt = target.data.bytes + fadt_offset;

    fadt[172] = 0;

    store_u64(fadt + 176, 0x1000);
    checksum_table(fadt, 244);

    ASTIA_CHECK(target.interpreter.build(target.tables));

    astia::acpi::detail::s5_plan     plan{};
    astia::acpi::detail::power_error error = astia::acpi::detail::power_error::none;

    ASTIA_CHECK(astia::acpi::detail::build_s5_plan(target.tables, target.interpreter,
                                                   plan, error));
    ASTIA_CHECK(plan.slp_typa == 5);

    write_log log{};

    log.memory_read_value = 0x0003;

    ASTIA_CHECK(astia::acpi::detail::execute_s5(
        plan, {&log, read_io, record_io, read_memory, record_memory}, error));
    ASTIA_CHECK(log.memory_read_count == 1);
    ASTIA_CHECK(log.memory_read_address == 0x1000);
    ASTIA_CHECK(log.memory_count == 1);
    ASTIA_CHECK(log.memory_address == 0x1000);
    ASTIA_CHECK(log.memory_value == 0x3403);
    ASTIA_CHECK(log.io_count == 0);
}

ASTIA_TEST(acpi_power_pm1_control_read_failure_does_not_write) {
    table_image target;

    make_fadt(target, false);

    ASTIA_CHECK(target.interpreter.build(target.tables));

    astia::acpi::detail::s5_plan     plan{};
    astia::acpi::detail::power_error error = astia::acpi::detail::power_error::none;

    ASTIA_CHECK(astia::acpi::detail::build_s5_plan(target.tables, target.interpreter,
                                                   plan, error));

    write_log log{};

    log.io_read_ok = false;

    ASTIA_CHECK(!astia::acpi::detail::execute_s5(
        plan, {&log, read_io, record_io, read_memory, record_memory}, error));
    ASTIA_CHECK(error == astia::acpi::detail::power_error::read_failed);
    ASTIA_CHECK(log.io_read_count == 1);
    ASTIA_CHECK(log.io_count == 0);
    ASTIA_CHECK(log.memory_count == 0);
}

ASTIA_TEST(acpi_power_pm1_control_write_failure_reports_error) {
    table_image target;

    make_fadt(target, false);

    ASTIA_CHECK(target.interpreter.build(target.tables));

    astia::acpi::detail::s5_plan     plan{};
    astia::acpi::detail::power_error error = astia::acpi::detail::power_error::none;

    ASTIA_CHECK(astia::acpi::detail::build_s5_plan(target.tables, target.interpreter,
                                                   plan, error));

    write_log log{};

    log.io_write_ok = false;

    ASTIA_CHECK(!astia::acpi::detail::execute_s5(
        plan, {&log, read_io, record_io, read_memory, record_memory}, error));
    ASTIA_CHECK(error == astia::acpi::detail::power_error::write_failed);
    ASTIA_CHECK(log.io_read_count == 1);
    ASTIA_CHECK(log.io_count == 1);
}

ASTIA_TEST(acpi_power_malformed_gas_rejects) {
    table_image target;

    make_fadt(target, true);

    u8* fadt = target.data.bytes + fadt_offset;

    fadt[173] = 8;

    checksum_table(fadt, 244);

    ASTIA_CHECK(target.interpreter.build(target.tables));

    astia::acpi::detail::s5_plan     plan{};
    astia::acpi::detail::power_error error = astia::acpi::detail::power_error::none;

    ASTIA_CHECK(!astia::acpi::detail::build_s5_plan(target.tables, target.interpreter,
                                                    plan, error));
    ASTIA_CHECK(error == astia::acpi::detail::power_error::invalid_pm1a);
}

ASTIA_TEST(acpi_power_reset_register_supports_io_and_memory) {
    table_image target;

    make_reset_fadt(target, astia::acpi::detail::gas_system_io, 0x64, 0x0e);

    astia::acpi::detail::reset_plan  plan{};
    astia::acpi::detail::power_error error = astia::acpi::detail::power_error::none;

    ASTIA_CHECK(astia::acpi::detail::build_reset_plan(target.tables, plan, error));
    ASTIA_CHECK(plan.valid && plan.value == 0x0e);

    reset_log log{};

    ASTIA_CHECK(astia::acpi::detail::execute_reset(
        plan, {&log, record_io8, record_memory8}, error));
    ASTIA_CHECK(log.io_count == 1 && log.io_port == 0x64 && log.io_value == 0x0e);

    make_reset_fadt(target, astia::acpi::detail::gas_system_memory, 0x2000, 0x0f);

    ASTIA_CHECK(astia::acpi::detail::build_reset_plan(target.tables, plan, error));
    ASTIA_CHECK(astia::acpi::detail::execute_reset(
        plan, {&log, record_io8, record_memory8}, error));
    ASTIA_CHECK(log.memory_count == 1 && log.memory_address == 0x2000 &&
                log.memory_value == 0x0f);
}

ASTIA_TEST(acpi_power_reset_register_pci_config_is_spec_valid_but_unsupported) {
    table_image target;

    const u64 config = (7ull << 32) | (2ull << 16) | 0x23;

    make_reset_fadt(target, astia::acpi::detail::gas_pci_configuration, config, 0x0d);

    astia::acpi::detail::reset_plan  plan{};
    astia::acpi::detail::power_error error = astia::acpi::detail::power_error::none;

    ASTIA_CHECK(!astia::acpi::detail::build_reset_plan(target.tables, plan, error));
    ASTIA_CHECK(error == astia::acpi::detail::power_error::unsupported_address_space);
    ASTIA_CHECK(!plan.valid);

    reset_log log{};

    ASTIA_CHECK(!astia::acpi::detail::execute_reset(
        plan, {&log, record_io8, record_memory8}, error));
    ASTIA_CHECK(log.io_count == 0 && log.memory_count == 0);
}

ASTIA_TEST(acpi_power_reset_register_requires_supported_flag_and_valid_fields) {
    table_image target;

    make_reset_fadt(target, astia::acpi::detail::gas_system_io, 0x64, 0x0e, false);

    astia::acpi::detail::reset_plan  plan{};
    astia::acpi::detail::power_error error = astia::acpi::detail::power_error::none;

    ASTIA_CHECK(!astia::acpi::detail::build_reset_plan(target.tables, plan, error));
    ASTIA_CHECK(error == astia::acpi::detail::power_error::invalid_reset);

    make_reset_fadt(target, astia::acpi::detail::gas_system_io, 0x64, 0x0e);

    target.data.bytes[fadt_offset + 117] = 16;

    checksum_table(target.data.bytes + fadt_offset, 244);

    ASTIA_CHECK(!astia::acpi::detail::build_reset_plan(target.tables, plan, error));
    ASTIA_CHECK(error == astia::acpi::detail::power_error::unsupported_access_size);

    make_reset_fadt(target, astia::acpi::detail::gas_pci_configuration,
                    (1ull << 48) | (2ull << 32) | 0x23, 0x0d);

    ASTIA_CHECK(!astia::acpi::detail::build_reset_plan(target.tables, plan, error));
    ASTIA_CHECK(error == astia::acpi::detail::power_error::invalid_reset);
}

ASTIA_TEST(acpi_power_reset_register_pci_address_boundaries) {
    table_image target;

    astia::acpi::detail::reset_plan  plan{};
    astia::acpi::detail::power_error error = astia::acpi::detail::power_error::none;

    const auto check_config = [&](u64                              address,
                                  astia::acpi::detail::power_error expected) {
        make_reset_fadt(target, astia::acpi::detail::gas_pci_configuration, address,
                        0x0d);

        ASTIA_CHECK(!astia::acpi::detail::build_reset_plan(target.tables, plan, error));
        ASTIA_CHECK(error == expected);
        ASTIA_CHECK(!plan.valid);
    };

    check_config((31ull << 32) | (7ull << 16) | 0xFFF,
                 astia::acpi::detail::power_error::unsupported_address_space);
    check_config((32ull << 32) | (0ull << 16) | 0,
                 astia::acpi::detail::power_error::invalid_reset);
    check_config((0ull << 32) | (8ull << 16) | 0,
                 astia::acpi::detail::power_error::invalid_reset);
    check_config((0ull << 32) | (0ull << 16) | 0x1000,
                 astia::acpi::detail::power_error::invalid_reset);
    check_config(1ull << 48, astia::acpi::detail::power_error::invalid_reset);

    // A conventional ECAM bus-1 address is not a valid GAS PCI address: the
    // GAS encoding has no bus field and requires segment group 0, bus 0.
    check_config(1ull << 20, astia::acpi::detail::power_error::invalid_reset);
}

ASTIA_TEST(acpi_power_reset_plan_is_independent_of_aml) {
    table_image target;

    make_reset_fadt(target, astia::acpi::detail::gas_system_io, 0x64, 0x0e);

    target.tables.dsdt = {
        astia::span<const u8>(truncated_aml, sizeof(truncated_aml)),
        image_base + dsdt_offset,
    };

    ASTIA_CHECK(!target.interpreter.build(target.tables));

    astia::acpi::detail::reset_plan  plan{};
    astia::acpi::detail::power_error error = astia::acpi::detail::power_error::none;

    ASTIA_CHECK(astia::acpi::detail::build_reset_plan(target.tables, plan, error));
    ASTIA_CHECK(plan.valid && plan.gas.space == astia::acpi::detail::gas_system_io);
}

ASTIA_TEST(acpi_power_invalid_sleep_type_rejects) {
    table_image target;

    const usz dsdt_length = add_table(target.data, dsdt_offset, "DSDT", 2,
                                      name_s5_invalid, sizeof(name_s5_invalid));
    const usz fadt_length = 116;

    add_table(target.data, fadt_offset, "FACP", 3, nullptr, fadt_length - 36);

    u8* fadt = target.data.bytes + fadt_offset;

    build_fadt_with_dsdt(image_base + dsdt_offset, fadt, fadt_length);

    target.tables.fadt = {
        astia::span<const u8>(fadt, fadt_length),
        image_base + fadt_offset,
    };
    target.tables.dsdt = {
        astia::span<const u8>(target.data.bytes + dsdt_offset, dsdt_length),
        image_base + dsdt_offset,
    };

    ASTIA_CHECK(target.interpreter.build(target.tables));

    astia::acpi::detail::s5_plan     plan{};
    astia::acpi::detail::power_error error = astia::acpi::detail::power_error::none;

    ASTIA_CHECK(!astia::acpi::detail::build_s5_plan(target.tables, target.interpreter,
                                                    plan, error));
    ASTIA_CHECK(error == astia::acpi::detail::power_error::invalid_sleep_type);
}

namespace {
    int shutdown_calls[8]{};
    int shutdown_count = 0;

    void reset_shutdown_log() {
        shutdown_count = 0;

        for (int& call : shutdown_calls) {
            call = 0;
        }
    }

    bool record_handler_1(void*) {
        shutdown_calls[shutdown_count++] = 1;

        return true;
    }

    bool record_handler_2(void*) {
        shutdown_calls[shutdown_count++] = 2;

        return false;
    }

    bool record_handler_3(void*) {
        shutdown_calls[shutdown_count++] = 3;

        return true;
    }

    bool record_handler_4(void*) {
        shutdown_calls[shutdown_count++] = 4;

        return true;
    }

    bool record_handler_5(void*) {
        shutdown_calls[shutdown_count++] = 5;

        return true;
    }

    bool late_registration_ok = true;

    bool record_handler_6(void*) {
        late_registration_ok = astia::shutdown::register_handler(
            astia::shutdown::tier::prepare, "late", record_handler_1);

        shutdown_calls[shutdown_count++] = 6;

        return true;
    }

    bool record_handler_7(void*) {
        shutdown_calls[shutdown_count++] = 7;

        return true;
    }

    bool record_handler_8(void*) {
        shutdown_calls[shutdown_count++] = 8;

        return true;
    }

    bool record_handler_9(void*) {
        shutdown_calls[shutdown_count++] = 9;

        return false;
    }
} // namespace

ASTIA_TEST(acpi_shutdown_tiers_are_ordered_and_failures_continue) {
    astia::shutdown::detail::reset_for_test();
    reset_shutdown_log();

    late_registration_ok = true;

    ASTIA_CHECK(astia::shutdown::register_handler(astia::shutdown::tier::final, "final",
                                                  record_handler_1));
    ASTIA_CHECK(astia::shutdown::register_handler(astia::shutdown::tier::platform,
                                                  "platform", record_handler_5));
    ASTIA_CHECK(astia::shutdown::register_handler(astia::shutdown::tier::prepare,
                                                  "prepare-one", record_handler_2));
    ASTIA_CHECK(astia::shutdown::register_handler(astia::shutdown::tier::prepare,
                                                  "prepare-two", record_handler_3));
    ASTIA_CHECK(astia::shutdown::register_handler(astia::shutdown::tier::flush, "flush",
                                                  record_handler_4));
    ASTIA_CHECK(astia::shutdown::register_handler(astia::shutdown::tier::quiesce,
                                                  "quiesce", record_handler_6));
    ASTIA_CHECK(!astia::shutdown::register_handler(astia::shutdown::tier::final,
                                                   "second-final", record_handler_5));

    ASTIA_CHECK(!astia::shutdown::initiate());
    ASTIA_CHECK(shutdown_count == 6);
    ASTIA_CHECK(shutdown_calls[0] == 2);
    ASTIA_CHECK(shutdown_calls[1] == 3);
    ASTIA_CHECK(shutdown_calls[2] == 4);
    ASTIA_CHECK(shutdown_calls[3] == 6);
    ASTIA_CHECK(shutdown_calls[4] == 5);
    ASTIA_CHECK(shutdown_calls[5] == 1);
    ASTIA_CHECK(!late_registration_ok);
    ASTIA_CHECK(!astia::shutdown::initiate());
    ASTIA_CHECK(!astia::shutdown::register_handler(astia::shutdown::tier::prepare, "late",
                                                   record_handler_1));
}

ASTIA_TEST(acpi_shutdown_requires_platform_handler) {
    astia::shutdown::detail::reset_for_test();
    reset_shutdown_log();

    ASTIA_CHECK(astia::shutdown::register_handler(astia::shutdown::tier::prepare,
                                                  "prepare", record_handler_1));

    ASTIA_CHECK(!astia::shutdown::initiate());
    ASTIA_CHECK(shutdown_count == 1);
}

ASTIA_TEST(acpi_shutdown_final_override_runs_after_tiers_and_skips_registered_final) {
    astia::shutdown::detail::reset_for_test();
    reset_shutdown_log();

    ASTIA_CHECK(astia::shutdown::register_handler(astia::shutdown::tier::final,
                                                  "registered-final", record_handler_8));
    ASTIA_CHECK(astia::shutdown::register_handler(astia::shutdown::tier::platform,
                                                  "platform", record_handler_5));
    ASTIA_CHECK(astia::shutdown::register_handler(astia::shutdown::tier::prepare,
                                                  "prepare", record_handler_1));
    ASTIA_CHECK(astia::shutdown::register_handler(astia::shutdown::tier::flush, "flush",
                                                  record_handler_3));
    ASTIA_CHECK(astia::shutdown::register_handler(astia::shutdown::tier::quiesce,
                                                  "quiesce", record_handler_4));

    ASTIA_CHECK(astia::shutdown::initiate(record_handler_7, nullptr));
    ASTIA_CHECK(shutdown_count == 5);
    ASTIA_CHECK(shutdown_calls[0] == 1);
    ASTIA_CHECK(shutdown_calls[1] == 3);
    ASTIA_CHECK(shutdown_calls[2] == 4);
    ASTIA_CHECK(shutdown_calls[3] == 5);
    ASTIA_CHECK(shutdown_calls[4] == 7);
}

ASTIA_TEST(acpi_shutdown_final_override_failure_stays_best_effort) {
    astia::shutdown::detail::reset_for_test();
    reset_shutdown_log();

    ASTIA_CHECK(astia::shutdown::register_handler(astia::shutdown::tier::final,
                                                  "registered-final", record_handler_8));
    ASTIA_CHECK(astia::shutdown::register_handler(astia::shutdown::tier::platform,
                                                  "platform", record_handler_5));
    ASTIA_CHECK(astia::shutdown::register_handler(astia::shutdown::tier::prepare,
                                                  "prepare", record_handler_1));

    ASTIA_CHECK(!astia::shutdown::initiate(record_handler_9, nullptr));
    ASTIA_CHECK(shutdown_count == 3);
    ASTIA_CHECK(shutdown_calls[0] == 1);
    ASTIA_CHECK(shutdown_calls[1] == 5);
    ASTIA_CHECK(shutdown_calls[2] == 9);
    ASTIA_CHECK(shutdown_calls[3] == 0);
}
