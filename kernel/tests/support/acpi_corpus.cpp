#include "acpi_corpus.hpp"

#include <cstdlib>
#include <vector>

namespace astia_test::acpi_corpus {
    namespace {
        using astia::acpi::detail::load_u32;
        using astia::acpi::detail::max_aml_nodes;
        using astia::acpi::detail::max_rsdp_length;
        using astia::acpi::detail::max_table_bytes;
        using astia::acpi::detail::max_tables;
        using astia::acpi::detail::rsdp_v2_size;
        using astia::acpi::detail::sdt_header_size;

        constexpr u64 image_base  = 0x100000;
        constexpr usz rsdp_offset = 0x000;
        constexpr usz root_offset = 0x080;
        constexpr usz fadt_offset = 0x200;
        constexpr usz dsdt_offset = 0x300;
        constexpr usz madt_offset = 0x300;

        constexpr usz table_image_used = dsdt_offset + sdt_header_size;
        constexpr usz madt_body_bytes  = 8 + 72;
        constexpr usz madt_image_used  = madt_offset + sdt_header_size + madt_body_bytes;
        constexpr usz madt_table_bytes = sdt_header_size + madt_body_bytes;
        constexpr usz entry_offset     = sdt_header_size + 8;
        constexpr usz padding_entries  = 32;

        u8 sum(const u8* bytes, usz length) {
            u8 value = 0;

            for (usz i = 0; i < length; ++i) {
                value = static_cast<u8>(value + bytes[i]);
            }

            return value;
        }

        void fix_checksum(u8* table, usz length) {
            table[9] = 0;
            table[9] = static_cast<u8>(0x100 - sum(table, length));
        }

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

        void store_pointer(u8* bytes, usz entry_size, u64 value) {
            if (entry_size == 8) {
                store_u64(bytes, value);

                return;
            }

            store_u32(bytes, static_cast<u32>(value));
        }

        u64 load_pointer(const u8* bytes, usz entry_size) {
            if (entry_size == 8)
                return astia::acpi::detail::load_u64(bytes);

            return load_u32(bytes);
        }

        void store_signature(u8* target, const char (&text)[5]) {
            for (usz i = 0; i < 4; ++i) {
                target[i] = static_cast<u8>(text[i]);
            }
        }

        usz write_table(u8* target, const char (&signature)[5], u8 revision,
                        const u8* body, usz body_length) {
            const usz length = sdt_header_size + body_length;

            store_signature(target, signature);
            store_u32(target + 4, static_cast<u32>(length));

            target[8]  = revision;
            target[9]  = 0;
            target[10] = 'A';
            target[11] = 'S';
            target[12] = 'T';
            target[13] = 'I';
            target[14] = 'A';

            for (usz i = 0; i < body_length; ++i) {
                target[sdt_header_size + i] = body != nullptr ? body[i] : 0;
            }

            fix_checksum(target, length);

            return length;
        }

        void write_rsdp(u8* target, bool extended, u64 root) {
            for (usz i = 0; i < 8; ++i) {
                target[i] = static_cast<u8>("RSD PTR "[i]);
            }

            target[8]  = 0;
            target[15] = extended ? 2 : 0;

            store_u32(target + 16, extended ? 0 : static_cast<u32>(root));

            target[8] = static_cast<u8>(0x100 - sum(target, 20));

            if (!extended)
                return;

            store_u32(target + 20, rsdp_v2_size);
            store_u64(target + 24, root);

            target[32] = 0;
            target[32] = static_cast<u8>(0x100 - sum(target, rsdp_v2_size));
        }

        void write_local_apic(u8* target, u32 id, bool enabled) {
            target[0] = 0;
            target[1] = 8;
            target[2] = 0;
            target[3] = static_cast<u8>(id);

            store_u32(target + 4, enabled ? 1 : 0);
        }

        void write_x2apic(u8* target, u32 id, bool enabled) {
            target[0] = 9;
            target[1] = 16;
            target[2] = 0;
            target[3] = 0;

            store_u32(target + 4, id);
            store_u32(target + 8, enabled ? 1 : 0);
            store_u32(target + 12, id);
        }

        usz table_length(const image& target, usz offset) {
            return load_u32(target.data() + offset + 4);
        }

        usz root_entry_size(const image& target) {
            return load_u32(target.data() + root_offset) ==
                           astia::acpi::detail::xsdt_signature
                       ? 8
                       : 4;
        }

        void flip_byte(u8* bytes, usz offset) {
            bytes[offset] = static_cast<u8>(bytes[offset] ^ 0xFF);
        }

        void mutate_none(image&) {}

        void table_rsdp_signature(image& target) {
            flip_byte(target.data(), 0);
        }

        void table_rsdp_checksum(image& target) {
            flip_byte(target.data(), 8);
        }

        void table_rsdp_v2_length_below_min(image& target) {
            store_u32(target.data() + 20, rsdp_v2_size - 1);
        }

        void table_rsdp_v2_length_above_max(image& target) {
            store_u32(target.data() + 20, max_rsdp_length + 1);
        }

        void table_rsdp_v2_checksum(image& target) {
            flip_byte(target.data(), 32);
        }

        void table_rsdp_v2_zero_roots(image& target) {
            store_u32(target.data() + 16, 0);
            store_u64(target.data() + 24, 0);

            target.data()[8] = 0;
            target.data()[8] = static_cast<u8>(0x100 - sum(target.data(), 20));

            target.data()[32] = 0;
            target.data()[32] = static_cast<u8>(0x100 - sum(target.data(), rsdp_v2_size));
        }

        void root_signature(image& target) {
            u8* const root = target.data() + root_offset;

            flip_byte(root, 0);
            fix_checksum(root, table_length(target, root_offset));
        }

        void root_length_below_header(image& target) {
            store_u32(target.data() + root_offset + 4, sdt_header_size - 4);
        }

        void root_length_above_cap(image& target) {
            store_u32(target.data() + root_offset + 4, max_table_bytes + 1);
        }

        void root_length_beyond_image(image& target) {
            store_u32(target.data() + root_offset + 4, 4096);
        }

        void root_length_unaligned(image& target) {
            const usz length = sdt_header_size + root_entry_size(target) / 2;

            store_u32(target.data() + root_offset + 4, static_cast<u32>(length));
            fix_checksum(target.data() + root_offset, length);
        }

        void root_too_many_entries(image& target) {
            const usz length =
                sdt_header_size + root_entry_size(target) * (max_tables + 1);

            store_u32(target.data() + root_offset + 4, static_cast<u32>(length));
            fix_checksum(target.data() + root_offset, length);
        }

        void root_checksum(image& target) {
            flip_byte(target.data(), root_offset + 9);
        }

        void root_zero_entry(image& target) {
            u8* const root = target.data() + root_offset;

            store_pointer(root + sdt_header_size, root_entry_size(target), 0);
            fix_checksum(root, table_length(target, root_offset));
        }

        void root_unmapped_entry(image& target) {
            u8* const root = target.data() + root_offset;

            store_pointer(root + sdt_header_size, root_entry_size(target),
                          image_base + 0x200000);
            fix_checksum(root, table_length(target, root_offset));
        }

        void root_duplicate_entry(image& target) {
            u8* const root = target.data() + root_offset;

            const usz entry_size = root_entry_size(target);

            const u8* const first = root + sdt_header_size;

            store_pointer(root + sdt_header_size + entry_size, entry_size,
                          load_pointer(first, entry_size));
            fix_checksum(root, table_length(target, root_offset));
        }

        void root_self_entry(image& target) {
            u8* const root = target.data() + root_offset;

            store_pointer(root + sdt_header_size, root_entry_size(target),
                          image_base + root_offset);
            fix_checksum(root, table_length(target, root_offset));
        }

        void fadt_signature_zero(image& target) {
            u8* const table = target.data() + fadt_offset;

            for (usz i = 0; i < 4; ++i) {
                table[i] = 0;
            }

            fix_checksum(table, table_length(target, fadt_offset));
        }

        void fadt_length_below_header(image& target) {
            store_u32(target.data() + fadt_offset + 4, sdt_header_size - 16);
        }

        void fadt_checksum(image& target) {
            flip_byte(target.data(), fadt_offset + 9);
        }

        void fadt_missing(image& target) {
            u8* const root = target.data() + root_offset;

            const usz entry_size = root_entry_size(target);
            const usz length     = sdt_header_size + entry_size;

            store_u32(root + 4, static_cast<u32>(length));
            store_pointer(root + sdt_header_size, entry_size, image_base + dsdt_offset);
            fix_checksum(root, length);
        }

        void fadt_dsdt_absent(image& target) {
            u8* const root = target.data() + root_offset;

            const usz entry_size = root_entry_size(target);
            const usz length     = sdt_header_size + entry_size;

            store_u32(root + 4, static_cast<u32>(length));
            store_pointer(root + sdt_header_size, entry_size, image_base + fadt_offset);
            fix_checksum(root, length);

            store_u32(target.data() + fadt_offset + 40, 0);
            fix_checksum(target.data() + fadt_offset, table_length(target, fadt_offset));
        }

        void dsdt_length_beyond_image(image& target) {
            store_u32(target.data() + dsdt_offset + 4, 4096);
        }

        void dsdt_checksum(image& target) {
            flip_byte(target.data(), dsdt_offset + 9);
        }

        void madt_absent(image& target) {
            u8* const table = target.data() + madt_offset;

            store_signature(table, "FACP");
            fix_checksum(table, table_length(target, madt_offset));
        }

        void madt_length_below_header(image& target) {
            store_u32(target.data() + madt_offset + 4, sdt_header_size + 4);
        }

        void madt_length_above_cap(image& target) {
            store_u32(target.data() + madt_offset + 4, max_table_bytes + 1);
        }

        void madt_checksum(image& target) {
            flip_byte(target.data(), madt_offset + 10);
        }

        void madt_trailing_byte(image& target) {
            const usz length = entry_offset + 1;

            store_u32(target.data() + madt_offset + 4, static_cast<u32>(length));
            fix_checksum(target.data() + madt_offset, length);
        }

        void madt_entry_length_zero(image& target) {
            target.data()[madt_offset + entry_offset + 1] = 0;

            fix_checksum(target.data() + madt_offset, madt_table_bytes);
        }

        void madt_entry_length_one(image& target) {
            target.data()[madt_offset + entry_offset + 1] = 1;

            fix_checksum(target.data() + madt_offset, madt_table_bytes);
        }

        void madt_entry_overruns_table(image& target) {
            const usz length = entry_offset + 8;

            target.data()[madt_offset + entry_offset + 1] = 16;

            store_u32(target.data() + madt_offset + 4, static_cast<u32>(length));
            fix_checksum(target.data() + madt_offset, length);
        }

        void madt_local_apic_short(image& target) {
            const usz length = entry_offset + 4;

            write_local_apic(target.data() + madt_offset + entry_offset, 0, true);

            target.data()[madt_offset + entry_offset + 1] = 4;

            store_u32(target.data() + madt_offset + 4, static_cast<u32>(length));
            fix_checksum(target.data() + madt_offset, length);
        }

        void madt_x2apic_short(image& target) {
            const usz length = entry_offset + 16;

            write_x2apic(target.data() + madt_offset + entry_offset, 1, true);

            target.data()[madt_offset + entry_offset + 1] = 8;

            store_u32(target.data() + madt_offset + 4, static_cast<u32>(length));
            fix_checksum(target.data() + madt_offset, length);
        }

        void madt_duplicate_local_apic(image& target) {
            const usz length = entry_offset + 16;

            u8* const table = target.data() + madt_offset;

            write_local_apic(table + entry_offset, 0, true);
            write_local_apic(table + entry_offset + 8, 0, true);
            store_u32(table + 4, static_cast<u32>(length));
            fix_checksum(table, length);
        }

        void madt_duplicate_x2apic(image& target) {
            const usz length = entry_offset + 32;

            u8* const table = target.data() + madt_offset;

            write_x2apic(table + entry_offset, 1, true);
            write_x2apic(table + entry_offset + 16, 1, true);
            store_u32(table + 4, static_cast<u32>(length));
            fix_checksum(table, length);
        }

        void madt_no_enabled_cpu(image& target) {
            const usz length = entry_offset + 8;

            u8* const table = target.data() + madt_offset;

            write_local_apic(table + entry_offset, 0, false);
            store_u32(table + 4, static_cast<u32>(length));
            fix_checksum(table, length);
        }

        void madt_capacity_exceeded(image& target) {
            const usz length = entry_offset + 16;

            u8* const table = target.data() + madt_offset;

            write_local_apic(table + entry_offset, 0, true);
            write_local_apic(table + entry_offset + 8, 1, true);
            store_u32(table + 4, static_cast<u32>(length));
            fix_checksum(table, length);
        }

        constexpr u8 truncated_extended_aml[]   = {0x5B};
        constexpr u8 unsupported_extended_aml[] = {0x5B, 0x2A};
        constexpr u8 truncated_byte_aml[]       = {0x08, 'T', 'E', 'S', 'T', 0x0A};
        constexpr u8 truncated_word_aml[]       = {0x08, 'T', 'E', 'S', 'T', 0x0B};
        constexpr u8 truncated_dword_aml[]      = {0x08, 'T', 'E', 'S', 'T', 0x0C};
        constexpr u8 truncated_qword_aml[]      = {0x08, 'T', 'E', 'S', 'T', 0x0E};
        constexpr u8 truncated_string_aml[] = {0x08, 'T', 'E', 'S', 'T', 0x0D, 'A', 'B'};
        constexpr u8 zero_package_length_aml[]  = {0x08, 'T', 'E', 'S', 'T', 0x12, 0x00};
        constexpr u8 truncated_package_aml[]    = {0x08, 'T', 'E', 'S', 'T', 0x12, 0x40};
        constexpr u8 oversized_package_aml[]    = {0x08, 'T',  'E',  'S',  'T',
                                                   0x12, 0xC0, 0xFF, 0xFF, 0xFF};
        constexpr u8 package_mismatch_aml[]     = {0x08, 'T',  'E',  'S',  'T',  0x12,
                                                   0x05, 0x01, 0x00, 0x00, 0x00, 0x00};
        constexpr u8 package_item_overrun_aml[] = {0x08, 'T',  'E',  'S',  'T',
                                                   0x12, 0x04, 0x03, 0x00, 0x00};
        constexpr u8 invalid_name_lead_aml[]    = {0x08, 'a', 'B', 'C', 'D', 0x00};
        constexpr u8 multiname_zero_aml[]       = {0x08, 0x2F, 0x00, 0x00};
        constexpr u8 oversized_buffer_aml[]     = {0x08, 'T',  'E',  'S',  'T',
                                                   0x11, 0x04, 0x0A, 0xFF, 0x00};
        constexpr u8 truncated_method_aml[]     = {0x14, 0x40, 'M', 'T', 'H', 'D', 0x00};

        constexpr u8 valid_s5_package[] = {
            0x08, 0x5F, 0x53, 0x35, 0x5F, 0x12, 0x06, 0x04, 0x00, 0x00, 0x00, 0x00,
        };

        u8 base36_digit(u32 value) {
            return static_cast<u8>(value < 26 ? 'A' + value : '0' + (value - 26));
        }

        void write_base36_name(u8* target, u32 index) {
            target[0] = 'A';
            target[1] = base36_digit((index / 1296) % 36);
            target[2] = base36_digit((index / 36) % 36);
            target[3] = base36_digit(index % 36);
        }

        void write_decimal_name(u8* target, u32 index) {
            target[0] = 'A';
            target[1] = static_cast<u8>('0' + (index / 100) % 10);
            target[2] = static_cast<u8>('0' + (index / 10) % 10);
            target[3] = static_cast<u8>('0' + index % 10);
        }

        usz generate_deep_packages(u8* out, usz capacity) {
            u8  nested[256]{};
            usz length = 0;

            for (u32 depth = 0; depth < 40; ++depth) {
                u8  wrapped[256]{};
                usz cursor = 0;

                const usz content        = (depth == 0) ? 2 : 1 + length;
                const usz length_bytes   = (1 + content) < 0x40 ? 1 : 2;
                const usz package_length = length_bytes + content;

                wrapped[cursor++] = 0x12;

                if (length_bytes == 1) {
                    wrapped[cursor++] = static_cast<u8>(package_length);
                }
                else {
                    wrapped[cursor++] = static_cast<u8>(0x40 | (package_length & 0x0F));
                    wrapped[cursor++] = static_cast<u8>((package_length >> 4) & 0xFF);
                }

                wrapped[cursor++] = 0x01;

                if (depth == 0) {
                    wrapped[cursor++] = 0x00;
                }
                else {
                    for (usz i = 0; i < length; ++i) {
                        wrapped[cursor++] = nested[i];
                    }
                }

                length = cursor;

                for (usz i = 0; i < length; ++i) {
                    nested[i] = wrapped[i];
                }
            }

            const usz total = 5 + length;
            if (total > capacity)
                return 0;

            out[0] = 0x08;
            out[1] = 'T';
            out[2] = 'E';
            out[3] = 'S';
            out[4] = 'T';

            for (usz i = 0; i < length; ++i) {
                out[5 + i] = nested[i];
            }

            return total;
        }

        usz generate_node_exhaustion(u8* out, usz capacity) {
            usz length = 0;

            for (u32 index = 0; index < max_aml_nodes + 64; ++index) {
                if (length + 6 > capacity)
                    break;

                out[length++] = 0x08;

                write_base36_name(out + length, index);

                length += 4;
                out[length++] = 0x00;
            }

            return length;
        }

        usz generate_value_exhaustion(u8* out, usz capacity) {
            usz length = 0;

            while (length + 6 <= capacity) {
                if (length / 6 == 4200)
                    break;

                out[length++] = 0x08;
                out[length++] = 'T';
                out[length++] = 'E';
                out[length++] = 'S';
                out[length++] = 'T';
                out[length++] = 0x00;
            }

            return length;
        }

        usz generate_deep_term(u8* out, usz capacity) {
            constexpr usz lnot_count    = 80;
            constexpr usz body_bytes    = 1 + lnot_count + 1;
            constexpr usz method_total  = 1 + 2 + 4 + 1 + body_bytes;
            constexpr usz package_value = method_total - 1;

            if (capacity < method_total)
                return 0;

            usz length = 0;

            out[length++] = 0x14;
            out[length++] = static_cast<u8>(0x40 | (package_value & 0x0F));
            out[length++] = static_cast<u8>(package_value >> 4);
            out[length++] = 0x5F;
            out[length++] = 0x53;
            out[length++] = 0x35;
            out[length++] = 0x5F;
            out[length++] = 0x00;
            out[length++] = 0xA4;

            for (usz i = 0; i < lnot_count; ++i) {
                out[length++] = 0x92;
            }

            out[length++] = 0x00;

            return length;
        }

        usz generate_execution_limit(u8* out, usz capacity) {
            constexpr u8 method[] = {
                0x14, 0x0D, 0x5F, 0x53, 0x35, 0x5F, 0x00,
                0x70, 0x01, 0x60, 0xA2, 0x03, 0x60, 0xA3,
            };

            if (capacity < sizeof(method))
                return 0;

            for (usz i = 0; i < sizeof(method); ++i) {
                out[i] = method[i];
            }

            return sizeof(method);
        }

        usz generate_reference_chain(u8* out, usz capacity) {
            constexpr u32 link_count = 20;
            constexpr usz total      = link_count * 9 + 10;

            if (capacity < total)
                return 0;

            usz length = 0;

            for (u32 link = 1; link <= link_count; ++link) {
                out[length++] = 0x06;

                write_decimal_name(out + length, link - 1);

                length += 4;

                write_decimal_name(out + length, link);

                length += 4;
            }

            out[length++] = 0x08;
            out[length++] = 0x5F;
            out[length++] = 0x53;
            out[length++] = 0x35;
            out[length++] = 0x5F;

            write_decimal_name(out + length, link_count);

            length += 4;

            return length;
        }

        const table_case table_case_list[] = {
            {"rsdp_absent", fixture_kind::rsdp_v1, mutate_none, table_error::no_rsdp, 0},
            {"rsdp_truncated", fixture_kind::rsdp_v1, mutate_none, table_error::no_rsdp,
             image_base + table_image_used - 10},
            {"rsdp_signature", fixture_kind::rsdp_v1, table_rsdp_signature,
             table_error::bad_rsdp_signature},
            {"rsdp_v1_checksum", fixture_kind::rsdp_v1, table_rsdp_checksum,
             table_error::bad_rsdp_checksum},
            {"rsdp_v2_length_below_min", fixture_kind::rsdp_v2,
             table_rsdp_v2_length_below_min, table_error::bad_rsdp_length},
            {"rsdp_v2_length_above_max", fixture_kind::rsdp_v2,
             table_rsdp_v2_length_above_max, table_error::bad_rsdp_length},
            {"rsdp_v2_checksum_reports_v1_length", fixture_kind::rsdp_v2,
             table_rsdp_v2_checksum, table_error::bad_rsdp_length},
            {"rsdp_v2_zero_roots", fixture_kind::rsdp_v2, table_rsdp_v2_zero_roots,
             table_error::bad_root_table},
            {"rsdt_bad_signature", fixture_kind::rsdp_v1, root_signature,
             table_error::bad_root_table},
            {"rsdt_length_below_header", fixture_kind::rsdp_v1, root_length_below_header,
             table_error::bad_table_length},
            {"rsdt_length_above_cap", fixture_kind::rsdp_v1, root_length_above_cap,
             table_error::bad_table_length},
            {"rsdt_length_beyond_image", fixture_kind::rsdp_v1, root_length_beyond_image,
             table_error::truncated},
            {"rsdt_length_unaligned", fixture_kind::rsdp_v1, root_length_unaligned,
             table_error::bad_table_length},
            {"rsdt_too_many_entries", fixture_kind::rsdp_v1, root_too_many_entries,
             table_error::too_many_tables},
            {"rsdt_bad_checksum", fixture_kind::rsdp_v1, root_checksum,
             table_error::bad_table_checksum},
            {"rsdt_zero_entry", fixture_kind::rsdp_v1, root_zero_entry,
             table_error::bad_root_table},
            {"rsdt_unmapped_entry", fixture_kind::rsdp_v1, root_unmapped_entry,
             table_error::truncated},
            {"rsdt_duplicate_entry", fixture_kind::rsdp_v1, root_duplicate_entry,
             table_error::duplicate_table},
            {"rsdt_self_entry", fixture_kind::rsdp_v1, root_self_entry,
             table_error::duplicate_table},
            {"fadt_signature_zero", fixture_kind::rsdp_v1, fadt_signature_zero,
             table_error::bad_table_signature},
            {"fadt_length_below_header", fixture_kind::rsdp_v1, fadt_length_below_header,
             table_error::bad_table_length},
            {"fadt_bad_checksum", fixture_kind::rsdp_v1, fadt_checksum,
             table_error::bad_table_checksum},
            {"fadt_missing", fixture_kind::rsdp_v1, fadt_missing, table_error::no_fadt},
            {"fadt_dsdt_absent", fixture_kind::rsdp_v1, fadt_dsdt_absent,
             table_error::no_dsdt},
            {"dsdt_length_beyond_image", fixture_kind::rsdp_v1, dsdt_length_beyond_image,
             table_error::truncated},
            {"dsdt_bad_checksum", fixture_kind::rsdp_v1, dsdt_checksum,
             table_error::bad_table_checksum},
            {"xsdt_bad_signature", fixture_kind::rsdp_v2, root_signature,
             table_error::bad_root_table},
            {"xsdt_length_unaligned", fixture_kind::rsdp_v2, root_length_unaligned,
             table_error::bad_table_length},
            {"xsdt_too_many_entries", fixture_kind::rsdp_v2, root_too_many_entries,
             table_error::too_many_tables},
            {"xsdt_bad_checksum", fixture_kind::rsdp_v2, root_checksum,
             table_error::bad_table_checksum},
            {"xsdt_zero_entry", fixture_kind::rsdp_v2, root_zero_entry,
             table_error::bad_root_table},
            {"xsdt_unmapped_entry", fixture_kind::rsdp_v2, root_unmapped_entry,
             table_error::truncated},
            {"xsdt_duplicate_entry", fixture_kind::rsdp_v2, root_duplicate_entry,
             table_error::duplicate_table},
        };

        const madt_case madt_case_list[] = {
            {"rsdp_signature", table_rsdp_signature, madt_error::bad_rsdp},
            {"rsdp_checksum", table_rsdp_checksum, madt_error::bad_rsdp},
            {"rsdp_v2_length_below_min", table_rsdp_v2_length_below_min,
             madt_error::bad_rsdp, 4, true},
            {"rsdp_v2_zero_roots", table_rsdp_v2_zero_roots, madt_error::no_madt, 4,
             true},
            {"root_bad_signature", root_signature, madt_error::bad_root_table},
            {"root_length_below_header", root_length_below_header,
             madt_error::bad_table_length},
            {"root_length_above_cap", root_length_above_cap,
             madt_error::bad_table_length},
            {"root_length_unaligned", root_length_unaligned, madt_error::bad_root_table},
            {"root_too_many_entries", root_too_many_entries, madt_error::bad_root_table},
            {"root_bad_checksum", root_checksum, madt_error::bad_table_checksum},
            {"root_zero_entry", root_zero_entry, madt_error::bad_root_table},
            {"root_unmapped_entry", root_unmapped_entry, madt_error::bad_root_table},
            {"madt_absent", madt_absent, madt_error::no_madt},
            {"madt_length_below_header", madt_length_below_header,
             madt_error::bad_table_length},
            {"madt_length_above_cap", madt_length_above_cap,
             madt_error::bad_table_length},
            {"madt_bad_checksum", madt_checksum, madt_error::bad_table_checksum},
            {"madt_trailing_byte", madt_trailing_byte, madt_error::bad_entry},
            {"madt_entry_length_zero", madt_entry_length_zero, madt_error::bad_entry},
            {"madt_entry_length_one", madt_entry_length_one, madt_error::bad_entry},
            {"madt_entry_overruns_table", madt_entry_overruns_table,
             madt_error::bad_entry},
            {"madt_local_apic_short", madt_local_apic_short, madt_error::bad_entry},
            {"madt_x2apic_short", madt_x2apic_short, madt_error::bad_entry},
            {"madt_duplicate_local_apic", madt_duplicate_local_apic,
             madt_error::duplicate_apic_id},
            {"madt_duplicate_x2apic", madt_duplicate_x2apic,
             madt_error::duplicate_apic_id},
            {"madt_no_enabled_cpu", madt_no_enabled_cpu, madt_error::no_cpus},
            {"madt_capacity_exceeded", madt_capacity_exceeded, madt_error::too_many_cpus,
             1},
        };

        const aml_case aml_case_list[] = {
            {"truncated_extended_opcode", truncated_extended_aml,
             sizeof(truncated_extended_aml), nullptr, aml_error::truncated, false},
            {"unsupported_extended_opcode", unsupported_extended_aml,
             sizeof(unsupported_extended_aml), nullptr, aml_error::unsupported_opcode,
             false},
            {"truncated_byte_literal", truncated_byte_aml, sizeof(truncated_byte_aml),
             nullptr, aml_error::truncated, false},
            {"truncated_word_literal", truncated_word_aml, sizeof(truncated_word_aml),
             nullptr, aml_error::truncated, false},
            {"truncated_dword_literal", truncated_dword_aml, sizeof(truncated_dword_aml),
             nullptr, aml_error::truncated, false},
            {"truncated_qword_literal", truncated_qword_aml, sizeof(truncated_qword_aml),
             nullptr, aml_error::truncated, false},
            {"truncated_string", truncated_string_aml, sizeof(truncated_string_aml),
             nullptr, aml_error::truncated, false},
            {"zero_package_length", zero_package_length_aml,
             sizeof(zero_package_length_aml), nullptr, aml_error::package_length, false},
            {"truncated_package_length", truncated_package_aml,
             sizeof(truncated_package_aml), nullptr, aml_error::truncated, false},
            {"oversized_package_length", oversized_package_aml,
             sizeof(oversized_package_aml), nullptr, aml_error::truncated, false},
            {"package_trailing_mismatch", package_mismatch_aml,
             sizeof(package_mismatch_aml), nullptr, aml_error::package_length, false},
            {"package_item_overrun", package_item_overrun_aml,
             sizeof(package_item_overrun_aml), nullptr, aml_error::truncated, false},
            {"invalid_name_lead", invalid_name_lead_aml, sizeof(invalid_name_lead_aml),
             nullptr, aml_error::name, false},
            {"multiname_zero_count", multiname_zero_aml, sizeof(multiname_zero_aml),
             nullptr, aml_error::name, false},
            {"oversized_buffer", oversized_buffer_aml, sizeof(oversized_buffer_aml),
             nullptr, aml_error::package_length, false},
            {"truncated_method_package", truncated_method_aml,
             sizeof(truncated_method_aml), nullptr, aml_error::truncated, false},
            {"deep_package_nesting", nullptr, 0, generate_deep_packages,
             aml_error::package_length, false},
            {"node_exhaustion", nullptr, 0, generate_node_exhaustion,
             aml_error::object_limit, false},
            {"value_exhaustion", nullptr, 0, generate_value_exhaustion,
             aml_error::value_limit, false},
            {"deep_term_recursion", nullptr, 0, generate_deep_term,
             aml_error::recursion_limit, true},
            {"execution_limit", nullptr, 0, generate_execution_limit,
             aml_error::execution_limit, true},
            {"reference_chain_limit", nullptr, 0, generate_reference_chain,
             aml_error::wrong_type, true},
        };
    } // namespace

    image::~image() {
        std::free(_data);
    }

    bool image::read(u64 address, u8* destination, usz length) const {
        if (destination == nullptr || address < _base)
            return false;

        const u64 offset = address - _base;

        if (offset > _size || length > _size - offset)
            return false;

        if (length == 0)
            return true;

        __builtin_memcpy(destination, _data + offset, length);

        return true;
    }

    namespace {
        bool read_image(void* context, u64 address, u8* destination, usz length) {
            return static_cast<const image*>(context)->read(address, destination, length);
        }
    } // namespace

    memory_reader image::reader() const {
        return {
            const_cast<image*>(this),
            read_image,
        };
    }

    bool image::assign(const u8* bytes, usz size, u64 base) {
        u8* const fresh = static_cast<u8*>(std::malloc(size == 0 ? 1 : size));
        if (fresh == nullptr)
            return false;

        if (size != 0)
            __builtin_memcpy(fresh, bytes, size);

        std::free(_data);

        _data = fresh;
        _size = size;
        _base = base;

        return true;
    }

    bool build_table_image(image& out, bool extended) {
        std::vector<u8> storage(image_capacity, 0);

        u8* const data = storage.data();

        const u64 fadt_address  = image_base + fadt_offset;
        const u64 dsdt_address  = image_base + dsdt_offset;
        u8        fadt_body[80] = {};

        store_u32(fadt_body + 4, static_cast<u32>(dsdt_address));
        store_u32(fadt_body + 28, 0x604);

        fadt_body[53] = 2;

        write_table(data + fadt_offset, "FACP", 3, fadt_body, sizeof(fadt_body));
        write_table(data + dsdt_offset, "DSDT", 2, nullptr, 0);

        u8 entries[16] = {};

        if (extended) {
            store_u64(entries, fadt_address);
            store_u64(entries + 8, dsdt_address);
            write_table(data + root_offset, "XSDT", 1, entries, 16);
        }
        else {
            store_u32(entries, static_cast<u32>(fadt_address));
            store_u32(entries + 4, static_cast<u32>(dsdt_address));
            write_table(data + root_offset, "RSDT", 1, entries, 8);
        }

        write_rsdp(data + rsdp_offset, extended, image_base + root_offset);

        return out.assign(data, table_image_used, image_base);
    }

    bool build_madt_image(image& out, bool extended) {
        std::vector<u8> storage(image_capacity, 0);

        u8* const data = storage.data();

        const u64 madt_address          = image_base + madt_offset;
        u8        body[madt_body_bytes] = {};

        store_u32(body, 0xFEE00000);
        store_u32(body + 4, 1);

        u8* const entries = body + 8;

        write_local_apic(entries, 0, true);

        for (usz i = 0; i < padding_entries; ++i) {
            entries[8 + i * 2]     = 0x7F;
            entries[8 + i * 2 + 1] = 0x02;
        }

        write_table(data + madt_offset, "APIC", 5, body, sizeof(body));

        u8 entry[8] = {};

        if (extended) {
            store_u64(entry, madt_address);
            write_table(data + root_offset, "XSDT", 1, entry, 8);
        }
        else {
            store_u32(entry, static_cast<u32>(madt_address));
            write_table(data + root_offset, "RSDT", 1, entry, 4);
        }

        write_rsdp(data + rsdp_offset, extended, image_base + root_offset);

        return out.assign(data, madt_image_used, image_base);
    }

    bool build_aml_image(image& out, const u8* aml, usz length) {
        if (aml == nullptr && length != 0)
            return false;

        std::vector<u8> storage(sdt_header_size + length, 0);

        write_table(storage.data(), "DSDT", 2, aml, length);

        return out.assign(storage.data(), storage.size(), image_base);
    }

    span<const u8> valid_s5_aml() {
        return {valid_s5_package, sizeof(valid_s5_package)};
    }

    span<const table_case> table_cases() {
        return {
            table_case_list,
            sizeof(table_case_list) / sizeof(table_case_list[0]),
        };
    }

    span<const madt_case> madt_cases() {
        return {
            madt_case_list,
            sizeof(madt_case_list) / sizeof(madt_case_list[0]),
        };
    }

    span<const aml_case> aml_cases() {
        return {
            aml_case_list,
            sizeof(aml_case_list) / sizeof(aml_case_list[0]),
        };
    }
} // namespace astia_test::acpi_corpus
