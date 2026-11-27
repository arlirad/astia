#include <astia/boot/aarch64/fdt.hpp>
#include <astia/core/endian.hpp>

#include "support/test.hpp"

namespace {
    using namespace astia;
    using namespace astia::boot::aarch64;
    using namespace astia::boot::aarch64::virt;

    constexpr fdt::console_policy trusted_console{0x09000000ULL, 0x1000U};

    constexpr usz blob_capacity    = 4096;
    constexpr u32 header_size      = 40;
    constexpr u32 fdt_magic        = 0xd00dfeedU;
    constexpr usz strings_capacity = 512;

    void store_be32(u8* destination, u32 value) {
        core::be<u32> encoded{value};

        encoded.store(destination);
    }

    void store_be64(u8* destination, u64 value) {
        core::be<u64> encoded{value};

        encoded.store(destination);
    }

    class dtb_builder {
        public:
        void begin_node(const char* name) {
            token(0x00000001);
            name_bytes(name);
        }

        void end_node() {
            token(0x00000002);
        }

        void property(const char* name, const u8* value, u32 length) {
            token(0x00000003);
            put32(length);
            put32(string_offset(name));

            for (u32 index = 0; index < length; ++index) {
                structure_[structure_length_++] = value[index];
            }

            pad_structure();
        }

        void property_u32(const char* name, u32 value) {
            u8 bytes[4]{};

            store_be32(bytes, value);
            property(name, bytes, sizeof(bytes));
        }

        void property_string(const char* name, const char* value) {
            u32 length = 0;

            while (value[length] != '\0') {
                ++length;
            }

            property(name, reinterpret_cast<const u8*>(value), length + 1);
        }

        void property_empty(const char* name) {
            property(name, nullptr, 0);
        }

        void property_cells(const char* name, u32 address_cells, u32 size_cells,
                            u64 address, u64 size) {
            u8 bytes[(2 + 2) * 4]{};

            u32 offset = 0;

            for (u32 index = 0; index < address_cells; ++index) {
                store_be32(
                    bytes + offset,
                    static_cast<u32>(address >> (32 * (address_cells - 1 - index))));

                offset += 4;
            }

            for (u32 index = 0; index < size_cells; ++index) {
                store_be32(bytes + offset,
                           static_cast<u32>(size >> (32 * (size_cells - 1 - index))));

                offset += 4;
            }

            property(name, bytes, offset);
        }

        void property_reg_pair(const char* name, u64 first_base, u64 first_size,
                               u64 second_base, u64 second_size) {
            u8 bytes[32]{};

            store_be32(bytes + 0, static_cast<u32>(first_base >> 32));
            store_be32(bytes + 4, static_cast<u32>(first_base));
            store_be32(bytes + 8, static_cast<u32>(first_size >> 32));
            store_be32(bytes + 12, static_cast<u32>(first_size));
            store_be32(bytes + 16, static_cast<u32>(second_base >> 32));
            store_be32(bytes + 20, static_cast<u32>(second_base));
            store_be32(bytes + 24, static_cast<u32>(second_size >> 32));
            store_be32(bytes + 28, static_cast<u32>(second_size));
            property(name, bytes, sizeof(bytes));
        }

        void reserve(u64 address, u64 size) {
            reservations_[reservation_count_++] = {address, size};
        }

        void end_structure() {
            token(0x00000009);
        }

        [[nodiscard]] u32 build(u8* destination) const {
            const u32 reservation_bytes = (reservation_count_ + 1) * 16;
            const u32 structure_offset  = header_size + reservation_bytes;
            const u32 strings_offset    = structure_offset + structure_length_;
            const u32 total_size        = strings_offset + strings_length_;

            for (usz index = 0; index < blob_capacity; ++index) {
                destination[index] = 0;
            }

            store_be32(destination + 0, fdt_magic);
            store_be32(destination + 4, total_size);
            store_be32(destination + 8, structure_offset);
            store_be32(destination + 12, strings_offset);
            store_be32(destination + 16, header_size);
            store_be32(destination + 20, 17);
            store_be32(destination + 24, 16);
            store_be32(destination + 28, 0);
            store_be32(destination + 32, strings_length_);
            store_be32(destination + 36, structure_length_);

            for (u32 index = 0; index < reservation_count_; ++index) {
                u8* entry = destination + header_size + index * 16;

                store_be64(entry, reservations_[index].address);
                store_be64(entry + 8, reservations_[index].size);
            }

            for (u32 index = 0; index < structure_length_; ++index) {
                destination[structure_offset + index] = structure_[index];
            }

            for (u32 index = 0; index < strings_length_; ++index) {
                destination[strings_offset + index] = strings_[index];
            }

            return total_size;
        }

        private:
        struct reservation {
            u64 address = 0;
            u64 size    = 0;
        };

        void token(u32 value) {
            put32(value);
        }

        void put32(u32 value) {
            store_be32(structure_ + structure_length_, value);

            structure_length_ += 4;
        }

        void name_bytes(const char* name) {
            while (*name != '\0') {
                structure_[structure_length_++] = static_cast<u8>(*name++);
            }

            structure_[structure_length_++] = 0;

            pad_structure();
        }

        void pad_structure() {
            while ((structure_length_ & 3U) != 0) {
                structure_[structure_length_++] = 0;
            }
        }

        [[nodiscard]] u32 string_offset(const char* name) {
            const u32 offset = strings_length_;

            while (*name != '\0') {
                strings_[strings_length_++] = static_cast<char>(*name++);
            }

            strings_[strings_length_++] = '\0';

            return offset;
        }

        u8          structure_[2048]{};
        u32         structure_length_ = 0;
        char        strings_[strings_capacity]{};
        u32         strings_length_ = 0;
        reservation reservations_[8]{};
        u32         reservation_count_ = 0;
    };

    u32 build_basic(u8* blob) {
        dtb_builder builder;

        builder.begin_node("");
        builder.property_u32("#address-cells", 2);
        builder.property_u32("#size-cells", 2);

        builder.begin_node("memory@40000000");
        builder.property_string("device_type", "memory");
        builder.property_cells("reg", 2, 2, 0x40000000ULL, 0x10000000ULL);
        builder.end_node();

        builder.begin_node("pl011@9000000");
        builder.property_string("compatible", "arm,pl011");
        builder.property_cells("reg", 2, 2, 0x09000000ULL, 0x1000ULL);
        builder.end_node();

        builder.begin_node("aliases");
        builder.property_string("serial0", "/pl011@9000000");
        builder.end_node();

        builder.begin_node("chosen");
        builder.property_string("stdout-path", "serial0");
        builder.end_node();

        builder.end_node();
        builder.end_structure();

        return builder.build(blob);
    }

    ASTIA_TEST(aarch64_fdt_valid_tree) {
        alignas(8) u8 blob[blob_capacity]{};

        const u32 size = build_basic(blob);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::none);
        ASTIA_CHECK(info.dtb == blob);
        ASTIA_CHECK(info.dtb_size == size);
        ASTIA_CHECK(info.memory.count() == 1);
        ASTIA_CHECK(info.memory[0].base == 0x40000000ULL);
        ASTIA_CHECK(info.memory[0].size == 0x10000000ULL);
        ASTIA_CHECK(info.reserved.count() == 0);
        ASTIA_CHECK(info.console.base == 0x09000000ULL);
        ASTIA_CHECK(info.console.size == 0x1000U);
        ASTIA_CHECK(info.console.source == virt::console_source::device_tree);
        ASTIA_CHECK(info.console.path[0] == '/');
    }

    ASTIA_TEST(aarch64_fdt_parses_unaligned_blob) {
        alignas(8) u8 storage[blob_capacity + 1]{};
        u8*           blob = storage + 1;

        const u32 size = build_basic(blob);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::none);
        ASTIA_CHECK(info.dtb_size == size);
        ASTIA_CHECK(info.memory.count() == 1);
        ASTIA_CHECK(info.memory[0].base == 0x40000000ULL);
        ASTIA_CHECK(info.console.base == 0x09000000ULL);
    }

    ASTIA_TEST(aarch64_fdt_direct_stdout_path) {
        alignas(8) u8 blob[blob_capacity]{};
        dtb_builder   builder;

        builder.begin_node("");
        builder.property_u32("#address-cells", 2);
        builder.property_u32("#size-cells", 2);
        builder.begin_node("memory@40000000");
        builder.property_string("device_type", "memory");
        builder.property_cells("reg", 2, 2, 0x40000000ULL, 0x10000000ULL);
        builder.end_node();
        builder.begin_node("pl011@9000000");
        builder.property_string("compatible", "arm,pl011");
        builder.property_cells("reg", 2, 2, 0x09000000ULL, 0x1000ULL);
        builder.end_node();
        builder.begin_node("chosen");
        builder.property_string("stdout-path", "/pl011@9000000:115200n8");
        builder.end_node();
        builder.end_node();
        builder.end_structure();
        (void) builder.build(blob);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::none);
        ASTIA_CHECK(info.console.source == virt::console_source::device_tree);
        ASTIA_CHECK(info.console.base == 0x09000000ULL);
    }

    ASTIA_TEST(aarch64_fdt_root_reg_is_ignored) {
        alignas(8) u8 blob[blob_capacity]{};
        dtb_builder   builder;

        builder.begin_node("");
        builder.property_u32("#address-cells", 2);
        builder.property_u32("#size-cells", 2);

        u8 root_reg[16]{};

        store_be32(root_reg + 0, 0);
        store_be32(root_reg + 4, 0x1000U);
        store_be32(root_reg + 8, 0);
        store_be32(root_reg + 12, 0x1000U);
        builder.property("reg", root_reg, sizeof(root_reg));
        builder.begin_node("memory@40000000");
        builder.property_string("device_type", "memory");
        builder.property_cells("reg", 2, 2, 0x40000000ULL, 0x10000000ULL);
        builder.end_node();
        builder.end_node();
        builder.end_structure();
        (void) builder.build(blob);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::none);
        ASTIA_CHECK(info.memory.count() == 1);
    }

    ASTIA_TEST(aarch64_fdt_hostile_pl011_base_is_not_trusted) {
        alignas(8) u8 blob[blob_capacity]{};
        dtb_builder   builder;

        builder.begin_node("");
        builder.property_u32("#address-cells", 2);
        builder.property_u32("#size-cells", 2);
        builder.begin_node("memory@40000000");
        builder.property_string("device_type", "memory");
        builder.property_cells("reg", 2, 2, 0x40000000ULL, 0x10000000ULL);
        builder.end_node();
        builder.begin_node("pl011@40000000");
        builder.property_string("compatible", "arm,pl011");
        builder.property_cells("reg", 2, 2, 0x40000000ULL, 0x1000ULL);
        builder.end_node();
        builder.begin_node("chosen");
        builder.property_string("stdout-path", "/pl011@40000000");
        builder.end_node();
        builder.end_node();
        builder.end_structure();
        (void) builder.build(blob);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::none);
        ASTIA_CHECK(info.memory.count() == 1);
        ASTIA_CHECK(info.console.source == virt::console_source::platform_default);
        ASTIA_CHECK(info.console.base == 0);
        ASTIA_CHECK(info.console.path[0] == '\0');
    }

    ASTIA_TEST(aarch64_fdt_undersized_pl011_falls_back) {
        alignas(8) u8 blob[blob_capacity]{};
        dtb_builder   builder;

        builder.begin_node("");
        builder.property_u32("#address-cells", 2);
        builder.property_u32("#size-cells", 2);
        builder.begin_node("memory@40000000");
        builder.property_string("device_type", "memory");
        builder.property_cells("reg", 2, 2, 0x40000000ULL, 0x10000000ULL);
        builder.end_node();
        builder.begin_node("pl011@9000000");
        builder.property_string("compatible", "arm,pl011");
        builder.property_cells("reg", 2, 2, 0x09000000ULL, 0x20ULL);
        builder.end_node();
        builder.begin_node("chosen");
        builder.property_string("stdout-path", "/pl011@9000000");
        builder.end_node();
        builder.end_node();
        builder.end_structure();
        (void) builder.build(blob);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::none);
        ASTIA_CHECK(info.memory.count() == 1);
        ASTIA_CHECK(info.console.source == virt::console_source::platform_default);
        ASTIA_CHECK(info.console.base == 0);
    }

    ASTIA_TEST(aarch64_fdt_reserved_regions) {
        alignas(8) u8 blob[blob_capacity]{};
        dtb_builder   builder;

        builder.reserve(0x50000000ULL, 0x1000ULL);
        builder.begin_node("");
        builder.property_u32("#address-cells", 2);
        builder.property_u32("#size-cells", 2);
        builder.begin_node("memory@40000000");
        builder.property_string("device_type", "memory");
        builder.property_cells("reg", 2, 2, 0x40000000ULL, 0x10000000ULL);
        builder.end_node();
        builder.begin_node("reserved-memory");
        builder.property_u32("#address-cells", 2);
        builder.property_u32("#size-cells", 2);
        builder.begin_node("region@50000000");
        builder.property_cells("reg", 2, 2, 0x50000000ULL, 0x2000ULL);
        builder.end_node();
        builder.end_node();
        builder.end_node();
        builder.end_structure();
        (void) builder.build(blob);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::none);
        ASTIA_CHECK(info.reserved.count() == 2);
        ASTIA_CHECK(info.reserved[0].base == 0x50000000ULL);
        ASTIA_CHECK(info.reserved[0].size == 0x1000ULL);
        ASTIA_CHECK(info.reserved[1].base == 0x50000000ULL);
        ASTIA_CHECK(info.reserved[1].size == 0x2000ULL);
    }

    ASTIA_TEST(aarch64_fdt_rejects_bad_magic) {
        alignas(8) u8 blob[blob_capacity]{};

        const u32 size = build_basic(blob);

        blob[0] = 0;

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::bad_magic);
        ASTIA_CHECK(size > header_size);
    }

    ASTIA_TEST(aarch64_fdt_rejects_truncated_header) {
        alignas(8) u8 blob[blob_capacity]{};

        build_basic(blob);
        store_be32(blob + 4, 20);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::too_small);
    }

    ASTIA_TEST(aarch64_fdt_rejects_totalsize_beyond_window) {
        alignas(8) u8 blob[blob_capacity]{};

        build_basic(blob);
        store_be32(blob + 4, blob_capacity + 1);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::bad_bounds);
    }

    ASTIA_TEST(aarch64_fdt_rejects_structure_strings_overlap) {
        alignas(8) u8 blob[blob_capacity]{};

        build_basic(blob);

        const u32 structure = core::be<u32>::load(blob + 8);

        store_be32(blob + 12, structure);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::bad_bounds);
    }

    ASTIA_TEST(aarch64_fdt_rejects_header_rsvmap_overlap) {
        alignas(8) u8 blob[blob_capacity]{};

        build_basic(blob);
        store_be32(blob + 16, 8);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::bad_bounds);
    }

    ASTIA_TEST(aarch64_fdt_rejects_unterminated_reservation) {
        alignas(8) u8 blob[blob_capacity]{};

        build_basic(blob);
        store_be64(blob + header_size, 0x50000000ULL);
        store_be64(blob + header_size + 8, 0x1000ULL);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::bad_bounds);
    }

    ASTIA_TEST(aarch64_fdt_rejects_header_structure_overlap) {
        alignas(8) u8 blob[blob_capacity]{};

        build_basic(blob);
        store_be32(blob + 8, 8);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::bad_bounds);
    }

    ASTIA_TEST(aarch64_fdt_rejects_header_strings_overlap) {
        alignas(8) u8 blob[blob_capacity]{};

        build_basic(blob);
        store_be32(blob + 12, 8);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::bad_bounds);
    }

    ASTIA_TEST(aarch64_fdt_rejects_bad_structure_offset) {
        alignas(8) u8 blob[blob_capacity]{};

        build_basic(blob);
        store_be32(blob + 8, blob_capacity - 2);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::bad_alignment);
    }

    ASTIA_TEST(aarch64_fdt_rejects_bad_token) {
        alignas(8) u8 blob[blob_capacity]{};

        build_basic(blob);

        const u32 structure = core::be<u32>::load(blob + 8);

        store_be32(blob + structure, 0xdeadbeefU);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::bad_token);
    }

    ASTIA_TEST(aarch64_fdt_rejects_unterminated_structure) {
        alignas(8) u8 blob[blob_capacity]{};

        build_basic(blob);

        const u32 structure = core::be<u32>::load(blob + 8);
        const u32 length    = core::be<u32>::load(blob + 36);

        store_be32(blob + structure + length - 4, 0x00000004U);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::bad_structure);
    }

    ASTIA_TEST(aarch64_fdt_rejects_bad_property_string) {
        alignas(8) u8 blob[blob_capacity]{};

        build_basic(blob);

        const u32 structure   = core::be<u32>::load(blob + 8);
        const u32 strings     = core::be<u32>::load(blob + 12);
        const u32 strings_len = core::be<u32>::load(blob + 32);

        store_be32(blob + structure + 16, strings_len + 1);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::bad_string);
        ASTIA_CHECK(strings != 0);
    }

    ASTIA_TEST(aarch64_fdt_rejects_bad_cell_width) {
        alignas(8) u8 blob[blob_capacity]{};
        dtb_builder   builder;

        builder.begin_node("");
        builder.property_u32("#address-cells", 2);
        builder.property_u32("#size-cells", 3);
        builder.begin_node("memory@40000000");

        u8 reg[20]{};

        store_be32(reg + 0, 0);
        store_be32(reg + 4, 0x40000000U);
        store_be32(reg + 8, 0);
        store_be32(reg + 12, 0);
        store_be32(reg + 16, 0x10000000U);
        builder.property("reg", reg, sizeof(reg));
        builder.end_node();
        builder.end_node();
        builder.end_structure();
        (void) builder.build(blob);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::bad_cells);
    }

    ASTIA_TEST(aarch64_fdt_rejects_cell_overflow) {
        alignas(8) u8 blob[blob_capacity]{};
        dtb_builder   builder;

        builder.begin_node("");
        builder.property_u32("#address-cells", 2);
        builder.property_u32("#size-cells", 2);
        builder.begin_node("memory@40000000");
        builder.property_cells("reg", 2, 2, ~u64{0}, 2);
        builder.end_node();
        builder.end_node();
        builder.end_structure();
        (void) builder.build(blob);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::bad_cells);
    }

    ASTIA_TEST(aarch64_fdt_requires_memory_device_type) {
        alignas(8) u8 blob[blob_capacity]{};
        dtb_builder   builder;

        builder.begin_node("");
        builder.property_u32("#address-cells", 2);
        builder.property_u32("#size-cells", 2);
        builder.begin_node("memory@40000000");
        builder.property_cells("reg", 2, 2, 0x40000000ULL, 0x10000000ULL);
        builder.end_node();
        builder.end_node();
        builder.end_structure();
        (void) builder.build(blob);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::no_memory);
    }

    ASTIA_TEST(aarch64_fdt_rejects_wrong_memory_device_type) {
        alignas(8) u8 blob[blob_capacity]{};
        dtb_builder   builder;

        builder.begin_node("");
        builder.property_u32("#address-cells", 2);
        builder.property_u32("#size-cells", 2);
        builder.begin_node("memory@40000000");
        builder.property_string("device_type", "ram");
        builder.property_cells("reg", 2, 2, 0x40000000ULL, 0x10000000ULL);
        builder.end_node();
        builder.end_node();
        builder.end_structure();
        (void) builder.build(blob);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::no_memory);
    }

    ASTIA_TEST(aarch64_fdt_rejects_missing_memory) {
        alignas(8) u8 blob[blob_capacity]{};
        dtb_builder   builder;

        builder.begin_node("");
        builder.property_u32("#address-cells", 2);
        builder.property_u32("#size-cells", 2);
        builder.end_node();
        builder.end_structure();
        (void) builder.build(blob);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::no_memory);
    }

    ASTIA_TEST(aarch64_fdt_rejects_depth_overflow) {
        alignas(8) u8 blob[blob_capacity]{};
        dtb_builder   builder;

        builder.begin_node("");
        builder.property_u32("#address-cells", 2);
        builder.property_u32("#size-cells", 2);
        builder.begin_node("memory@40000000");
        builder.property_cells("reg", 2, 2, 0x40000000ULL, 0x10000000ULL);
        builder.end_node();

        for (u32 index = 0; index < 65; ++index) {
            builder.begin_node("n");
        }

        for (u32 index = 0; index < 65; ++index) {
            builder.end_node();
        }

        builder.end_node();
        builder.end_structure();
        (void) builder.build(blob);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::depth_overflow);
    }

    struct gic_options {
        const char* compatible              = "arm,gic-v3";
        bool        interrupt_controller    = true;
        bool        interrupt_cells_present = true;
        u32         interrupt_cells         = 3;
        bool        regions_present         = true;
        u32         redistributor_regions   = 1;
        bool        two_pairs               = true;
        bool        reg_before_compatible   = false;
        bool        include_its             = false;
        bool        duplicate               = false;
        u64         distributor_size        = 0x10000ULL;
        u64         redistributor_size      = 0xf60000ULL;
    };

    u32 build_gic_tree(u8* blob, const gic_options& options = {}) {
        dtb_builder builder;

        builder.begin_node("");
        builder.property_u32("#address-cells", 2);
        builder.property_u32("#size-cells", 2);

        builder.begin_node("memory@40000000");
        builder.property_string("device_type", "memory");
        builder.property_cells("reg", 2, 2, 0x40000000ULL, 0x10000000ULL);
        builder.end_node();

        for (u32 instance = 0; instance < (options.duplicate ? 2U : 1U); ++instance) {
            builder.begin_node("intc@8000000");

            if (options.reg_before_compatible) {
                if (options.two_pairs)
                    builder.property_reg_pair("reg", 0x08000000ULL,
                                              options.distributor_size, 0x080a0000ULL,
                                              options.redistributor_size);
                else
                    builder.property_cells("reg", 2, 2, 0x08000000ULL,
                                           options.distributor_size);
            }

            builder.property_string("compatible", options.compatible);

            if (options.interrupt_controller)
                builder.property_empty("interrupt-controller");

            if (options.interrupt_cells_present)
                builder.property_u32("#interrupt-cells", options.interrupt_cells);

            if (options.regions_present)
                builder.property_u32("#redistributor-regions",
                                     options.redistributor_regions);

            if (!options.reg_before_compatible) {
                if (options.two_pairs)
                    builder.property_reg_pair("reg", 0x08000000ULL,
                                              options.distributor_size, 0x080a0000ULL,
                                              options.redistributor_size);
                else
                    builder.property_cells("reg", 2, 2, 0x08000000ULL,
                                           options.distributor_size);
            }

            if (options.include_its) {
                builder.begin_node("its@8080000");
                builder.property_string("compatible", "arm,gic-v3-its");
                builder.property_cells("reg", 2, 2, 0x08080000ULL, 0x20000ULL);
                builder.end_node();
            }

            builder.end_node();
        }

        builder.end_node();
        builder.end_structure();

        return builder.build(blob);
    }

    ASTIA_TEST(aarch64_fdt_parses_gicv3) {
        alignas(8) u8 blob[blob_capacity]{};

        (void) build_gic_tree(blob);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::none);
        ASTIA_CHECK(info.gic.present);
        ASTIA_CHECK(info.gic.distributor_base == 0x08000000ULL);
        ASTIA_CHECK(info.gic.distributor_size == 0x10000ULL);
        ASTIA_CHECK(info.gic.redistributor_base == 0x080a0000ULL);
        ASTIA_CHECK(info.gic.redistributor_size == 0xf60000ULL);
        ASTIA_CHECK(info.gic.redistributor_regions == 1);
    }

    ASTIA_TEST(aarch64_fdt_gicv3_is_order_independent) {
        alignas(8) u8 blob[blob_capacity]{};
        gic_options   options;

        options.reg_before_compatible = true;

        (void) build_gic_tree(blob, options);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::none);
        ASTIA_CHECK(info.gic.present);
        ASTIA_CHECK(info.gic.distributor_base == 0x08000000ULL);
        ASTIA_CHECK(info.gic.redistributor_base == 0x080a0000ULL);
    }

    ASTIA_TEST(aarch64_fdt_gicv3_defaults_single_redistributor_region) {
        alignas(8) u8 blob[blob_capacity]{};
        gic_options   options;

        options.regions_present = false;

        (void) build_gic_tree(blob, options);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::none);
        ASTIA_CHECK(info.gic.present);
        ASTIA_CHECK(info.gic.redistributor_regions == 1);
    }

    ASTIA_TEST(aarch64_fdt_gicv3_its_child_is_ignored) {
        alignas(8) u8 blob[blob_capacity]{};
        gic_options   options;

        options.include_its = true;

        (void) build_gic_tree(blob, options);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::none);
        ASTIA_CHECK(info.gic.present);
        ASTIA_CHECK(info.gic.distributor_base == 0x08000000ULL);
    }

    ASTIA_TEST(aarch64_fdt_rejects_gicv3_without_interrupt_controller) {
        alignas(8) u8 blob[blob_capacity]{};
        gic_options   options;

        options.interrupt_controller = false;

        (void) build_gic_tree(blob, options);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::bad_gic);
    }

    ASTIA_TEST(aarch64_fdt_rejects_gicv3_wrong_interrupt_cells) {
        alignas(8) u8 blob[blob_capacity]{};
        gic_options   options;

        options.interrupt_cells = 2;

        (void) build_gic_tree(blob, options);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::bad_gic);
    }

    ASTIA_TEST(aarch64_fdt_rejects_gicv3_missing_interrupt_cells) {
        alignas(8) u8 blob[blob_capacity]{};
        gic_options   options;

        options.interrupt_cells_present = false;

        (void) build_gic_tree(blob, options);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::bad_gic);
    }

    ASTIA_TEST(aarch64_fdt_rejects_gicv3_multiple_redistributor_regions) {
        alignas(8) u8 blob[blob_capacity]{};
        gic_options   options;

        options.redistributor_regions = 2;

        (void) build_gic_tree(blob, options);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::bad_gic);
    }

    ASTIA_TEST(aarch64_fdt_rejects_gicv3_single_register_pair) {
        alignas(8) u8 blob[blob_capacity]{};
        gic_options   options;

        options.two_pairs = false;

        (void) build_gic_tree(blob, options);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::bad_gic);
    }

    ASTIA_TEST(aarch64_fdt_rejects_gicv3_unaligned_region) {
        alignas(8) u8 blob[blob_capacity]{};
        gic_options   options;

        options.distributor_size = 0x1001;

        (void) build_gic_tree(blob, options);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::bad_gic);
    }

    ASTIA_TEST(aarch64_fdt_rejects_legacy_gic) {
        alignas(8) u8 blob[blob_capacity]{};
        gic_options   options;

        options.compatible = "arm,cortex-a15-gic";

        (void) build_gic_tree(blob, options);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::bad_gic);
    }

    ASTIA_TEST(aarch64_fdt_rejects_duplicate_gicv3) {
        alignas(8) u8 blob[blob_capacity]{};
        gic_options   options;

        options.duplicate = true;

        (void) build_gic_tree(blob, options);

        virt::boot_info info{};

        ASTIA_CHECK(fdt::parse(blob, blob_capacity, trusted_console, info) ==
                    fdt::error::bad_gic);
    }
} // namespace
