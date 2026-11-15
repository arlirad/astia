#include "test.hpp"
#include "ext4/ext4_reader.hpp"

#include <cstring>
#include <vector>

namespace {

    using astia::u8;
    using astia::u16;
    using astia::u32;
    using astia::u64;

    struct image {
        std::vector<u8> bytes;

        image() : bytes(32 * 1024, 0) {
            put32(1024 + 0x00, 16);
            put32(1024 + 0x04, 32);
            put32(1024 + 0x18, 0);
            put32(1024 + 0x20, 32);
            put32(1024 + 0x28, 16);
            put16(1024 + 0x38, 0xEF53);
            put32(1024 + 0x5C, 0);
            put32(1024 + 0x60, 0x42);
            put32(1024 + 0x64, 0);
            put32(1024 + 0x150, 0);
            put16(1024 + 0x58, 128);
            put16(1024 + 0xFE, 32);
            put32(2048 + 0x08, 3);
            make_inode(3200, 040755, 2, 5);
            make_inode(4480, 0100644, 12, 6);
            make_directory();
            const char text[] = "hello from ext4\n";
            std::memcpy(bytes.data() + 6 * 1024, text, sizeof text - 1);
        }

        void put16(u64 offset, u16 value) {
            bytes[offset] = static_cast<u8>(value);
            bytes[offset + 1] = static_cast<u8>(value >> 8);
        }
        void put32(u64 offset, u32 value) {
            bytes[offset] = static_cast<u8>(value);
            bytes[offset + 1] = static_cast<u8>(value >> 8);
            bytes[offset + 2] = static_cast<u8>(value >> 16);
            bytes[offset + 3] = static_cast<u8>(value >> 24);
        }
        void make_inode(u64 offset, u16 mode, u32 number, u32 data_block) {
            put16(offset + 0x00, mode);
            put32(offset + 0x04, number == 12 ? 16 : 1024);
            put16(offset + 0x1A, 1);
            put32(offset + 0x20, 0x00080000);
            put32(offset + 0x64, 1);
            put16(offset + 0x28, 0xF30A);
            put16(offset + 0x2A, 1);
            put16(offset + 0x2C, 4);
            put16(offset + 0x2E, 0);
            put32(offset + 0x34, 0);
            put16(offset + 0x38, 1);
            put16(offset + 0x3A, 0);
            put32(offset + 0x3C, data_block);
        }
        void make_directory() {
            const u64 base = 5 * 1024;
            put32(base + 0, 2);
            put16(base + 4, 12);
            bytes[base + 6] = 1;
            bytes[base + 7] = 2;
            bytes[base + 8] = '.';
            put32(base + 12, 2);
            put16(base + 16, 12);
            bytes[base + 18] = 2;
            bytes[base + 19] = 2;
            bytes[base + 20] = '.';
            bytes[base + 21] = '.';
            put32(base + 24, 12);
            put16(base + 28, 1000);
            bytes[base + 30] = 9;
            bytes[base + 31] = 1;
            std::memcpy(bytes.data() + base + 32, "known.txt", 9);
        }
    };

    bool read_image(void* context, u64 offset, void* buffer, astia::usize length) {
        auto& image = *static_cast<struct image*>(context);
        if (offset > image.bytes.size() || length > image.bytes.size() - offset)
            return false;
        std::memcpy(buffer, image.bytes.data() + offset, length);
        return true;
    }

    struct directory_state {
        bool found = false;
        u32 inode = 0;
    };

    bool find_known(void* context, const astia::ext4::directory_entry& entry) {
        auto& state = *static_cast<directory_state*>(context);
        if (entry.name_length == 9 && std::memcmp(entry.name, "known.txt", 9) == 0) {
            state.found = true;
            state.inode = entry.inode;
        }
        return true;
    }

    void make_checksum_superblock(image& image) {
        const u64 base = 1024;
        image.put32(base + 0x64, 0x0400);
        image.bytes[base + 0x175] = 1;
        for (u32 i = 0; i < 16; ++i)
            image.bytes[base + 0x68 + i] = static_cast<u8>(i + 1);
        image.put32(base + 0x3FC, 0);
        image.put32(base + 0x3FC,
                    astia::ext4::reader::crc32c(~u32{0}, image.bytes.data() + base, 0x3FC));
    }

    void make_checksum_metadata(image& image) {
        make_checksum_superblock(image);
        const u32 seed = astia::ext4::reader::crc32c(
            ~u32{0}, image.bytes.data() + 1024 + 0x68, 16);

        u8 descriptor[32]{ };
        std::memcpy(descriptor, image.bytes.data() + 2048, sizeof descriptor);
        descriptor[0x1E] = descriptor[0x1F] = 0;
        const u32 descriptor_crc = astia::ext4::reader::crc32c(
            astia::ext4::reader::crc32c(seed, "\0\0\0\0", 4), descriptor,
            sizeof descriptor);
        image.put16(2048 + 0x1E, static_cast<u16>(descriptor_crc));

        for (u32 number : {2u, 12u}) {
            const u64 offset = number == 2 ? 3200 : 4480;
            u8 raw[128]{ };
            std::memcpy(raw, image.bytes.data() + offset, sizeof raw);
            raw[0x7C] = raw[0x7D] = 0;
            u8 number_bytes[4] = {static_cast<u8>(number), static_cast<u8>(number >> 8),
                                  static_cast<u8>(number >> 16), static_cast<u8>(number >> 24)};
            u32 checksum = astia::ext4::reader::crc32c(seed, number_bytes, 4);
            checksum = astia::ext4::reader::crc32c(checksum, raw + 0x64, 4);
            checksum = astia::ext4::reader::crc32c(checksum, raw, sizeof raw);
            image.put16(offset + 0x7C, static_cast<u16>(checksum));
        }
    }

    void update_extent_checksum(image& image) {
        const u64 extent = 8 * 1024;
        const u32 seed = astia::ext4::reader::crc32c(
            ~u32{0}, image.bytes.data() + 1024 + 0x68, 16);
        u8 number_bytes[4] = {12, 0, 0, 0};
        u8 generation_bytes[4] = {1, 0, 0, 0};
        u32 checksum = astia::ext4::reader::crc32c(seed, number_bytes, 4);
        checksum = astia::ext4::reader::crc32c(checksum, generation_bytes, 4);
        checksum = astia::ext4::reader::crc32c(checksum, image.bytes.data() + extent, 1020);
        image.put32(extent + 1020, checksum);
    }

    void make_extent_checksum_file(image& image) {
        image.put32(4480 + 0x04, 1024);
        image.put16(4480 + 0x2A, 1);
        image.put16(4480 + 0x2C, 4);
        image.put16(4480 + 0x2E, 1);
        image.put32(4480 + 0x30, 0);
        image.put32(4480 + 0x34, 0);
        image.put32(4480 + 0x38, 8);
        image.put16(4480 + 0x3C, 0);
        image.put16(4480 + 0x3E, 0);

        const u64 extent = 8 * 1024;
        image.put16(extent + 0, 0xF30A);
        image.put16(extent + 2, 1);
        image.put16(extent + 4, 84);
        image.put16(extent + 6, 0);
        image.put32(extent + 8, 0);
        image.put32(extent + 12, 0);
        image.put16(extent + 16, 1);
        image.put16(extent + 18, 0);
        image.put32(extent + 20, 9);
        std::memcpy(image.bytes.data() + 9 * 1024, "extent checksum\n", 16);
        make_checksum_metadata(image);
        update_extent_checksum(image);
    }

    void make_indexed_directory(image& image) {
        image.put32(1024 + 0x5C, 0x0020);
        image.put32(3200 + 0x04, 3 * 1024);
        image.put32(3200 + 0x20, 0x00081000);
        image.put16(3200 + 0x38, 3);
        image.put32(4480 + 0x3C, 8);
        std::memcpy(image.bytes.data() + 8 * 1024, "hello from ext4\n", 16);
        std::memset(image.bytes.data() + 5 * 1024, 0, 3 * 1024);

        const u64 root = 5 * 1024;
        image.put32(root, 2);
        image.put16(root + 4, 12);
        image.bytes[root + 6] = 1;
        image.bytes[root + 7] = 2;
        image.bytes[root + 8] = '.';
        image.put32(root + 12, 2);
        image.put16(root + 16, 1024 - 12);
        image.bytes[root + 18] = 2;
        image.bytes[root + 19] = 2;
        image.bytes[root + 20] = '.';
        image.bytes[root + 21] = '.';
        image.bytes[root + 0x1C] = 1;
        image.bytes[root + 0x1D] = 8;
        image.bytes[root + 0x1E] = 1;
        image.put16(root + 0x20, 100);
        image.put16(root + 0x22, 1);
        image.put32(root + 0x24, 1);

        const u64 node = 6 * 1024;
        image.put16(node + 4, 1024);
        image.put16(node + 8, 100);
        image.put16(node + 0xA, 1);
        image.put32(node + 0xE, 2);

        const u64 leaf = 7 * 1024;
        image.put32(leaf, 12);
        image.put16(leaf + 4, 1024);
        image.bytes[leaf + 6] = 9;
        image.bytes[leaf + 7] = 1;
        std::memcpy(image.bytes.data() + leaf + 8, "known.txt", 9);
    }

    void make_indexed_checksum_directory(image& image) {
        make_indexed_directory(image);
        make_checksum_superblock(image);
        const u32 seed = astia::ext4::reader::crc32c(
            ~u32{0}, image.bytes.data() + 1024 + 0x68, 16);

        u8 descriptor[32]{};
        std::memcpy(descriptor, image.bytes.data() + 2048, sizeof descriptor);
        descriptor[0x1E] = descriptor[0x1F] = 0;
        const u32 descriptor_crc = astia::ext4::reader::crc32c(
            astia::ext4::reader::crc32c(seed, "\0\0\0\0", 4), descriptor,
            sizeof descriptor);
        image.put16(2048 + 0x1E, static_cast<u16>(descriptor_crc));

        for (u32 number : {2u, 12u}) {
            const u64 offset = number == 2 ? 3200 : 4480;
            u8 raw[128]{};
            std::memcpy(raw, image.bytes.data() + offset, sizeof raw);
            raw[0x7C] = raw[0x7D] = 0;
            u8 number_bytes[4] = {static_cast<u8>(number), static_cast<u8>(number >> 8),
                                  static_cast<u8>(number >> 16), static_cast<u8>(number >> 24)};
            u32 checksum = astia::ext4::reader::crc32c(seed, number_bytes, 4);
            checksum = astia::ext4::reader::crc32c(checksum, raw + 0x64, 4);
            checksum = astia::ext4::reader::crc32c(checksum, raw, sizeof raw);
            image.put16(offset + 0x7C, static_cast<u16>(checksum));
        }

        image.put16(6 * 1024 + 8, 126);
        const u32 root_count_offset = 0x20;
        const u32 node_count_offset = 8;
        auto index_checksum = [&](u64 offset, u32 count_offset, u32 number) {
            u8 number_bytes[4] = {static_cast<u8>(number), static_cast<u8>(number >> 8),
                                  static_cast<u8>(number >> 16), static_cast<u8>(number >> 24)};
            u8 generation_bytes[4] = {1, 0, 0, 0};
            u8 tail[8]{};
            u32 checksum = astia::ext4::reader::crc32c(seed, number_bytes, 4);
            checksum = astia::ext4::reader::crc32c(checksum, generation_bytes, 4);
            checksum = astia::ext4::reader::crc32c(checksum,
                                                   image.bytes.data() + offset,
                                                   count_offset + 8);
            checksum = astia::ext4::reader::crc32c(checksum, tail, sizeof tail);
            image.put32(offset + 1020, checksum);
        };
        image.put16(5 * 1024 + 0x20, 123);
        index_checksum(5 * 1024, root_count_offset, 2);
        index_checksum(6 * 1024, node_count_offset, 2);

        const u64 leaf = 7 * 1024;
        image.put16(leaf + 4, 1012);
        image.put32(leaf + 1012, 0);
        image.put16(leaf + 1016, 12);
        image.bytes[leaf + 1018] = 0;
        image.bytes[leaf + 1019] = 0xDE;
        u8 number_bytes[4] = {2, 0, 0, 0};
        u8 generation_bytes[4] = {1, 0, 0, 0};
        u32 checksum = astia::ext4::reader::crc32c(seed, number_bytes, 4);
        checksum = astia::ext4::reader::crc32c(checksum, generation_bytes, 4);
        checksum = astia::ext4::reader::crc32c(checksum, image.bytes.data() + leaf, 1012);
        image.put32(leaf + 1020, checksum);
    }

} // namespace

ASTIA_TEST(ext4_reads_extent_file_and_directory) {
    image image_data;
    astia::ext4::reader reader;
    ASTIA_CHECK(reader.open(read_image, &image_data));
    ASTIA_CHECK(reader.block_size() == 1024);
    ASTIA_CHECK(reader.feature_incompat() == 0x42);

    astia::ext4::inode root;
    astia::ext4::inode file;
    ASTIA_CHECK(reader.read_inode(2, root));
    ASTIA_CHECK(reader.read_inode(12, file));
    ASTIA_CHECK(root.type() == astia::ext4::node_type::directory);
    ASTIA_CHECK(file.type() == astia::ext4::node_type::regular);

    char contents[32]{};
    ASTIA_CHECK(reader.read_file(file, 0, contents, sizeof contents));
    ASTIA_CHECK(std::strcmp(contents, "hello from ext4\n") == 0);

    directory_state state;
    ASTIA_CHECK(reader.read_directory(root, find_known, &state));
    ASTIA_CHECK(state.found);
    ASTIA_CHECK(state.inode == 12);
}

ASTIA_TEST(ext4_reads_single_indirect_block) {
    image image_data;
    image_data.put32(4480 + 0x20, 0);
    image_data.put32(4480 + 0x28, 0);
    image_data.put32(4480 + 0x28 + 48, 7);
    image_data.put32(4480 + 0x04, 12 * 1024 + 16);
    image_data.put32(7 * 1024, 6);
    astia::ext4::reader reader;
    astia::ext4::inode file;
    ASTIA_CHECK(reader.open(read_image, &image_data));
    ASTIA_CHECK(reader.read_inode(12, file));
    u64 physical = 0;
    ASTIA_CHECK(reader.map_file_block(file, 12, physical));
    ASTIA_CHECK(physical == 6);
    char contents[32]{};
    ASTIA_CHECK(reader.read_file(file, 12 * 1024, contents, sizeof contents));
    ASTIA_CHECK(std::strcmp(contents, "hello from ext4\n") == 0);
}

ASTIA_TEST(ext4_reads_double_indirect_boundary) {
    image image_data;
    image_data.put32(4480 + 0x20, 0);
    image_data.put32(4480 + 0x28, 0);
    image_data.put32(4480 + 0x28 + 52, 7);
    image_data.put32(7 * 1024, 8);
    image_data.put32(8 * 1024, 6);
    image_data.put32(4480 + 0x04, (12 + 256 + 1) * 1024 + 16);
    astia::ext4::reader reader;
    astia::ext4::inode file;
    ASTIA_CHECK(reader.open(read_image, &image_data));
    ASTIA_CHECK(reader.read_inode(12, file));
    u64 physical = 0;
    ASTIA_CHECK(reader.map_file_block(file, 12 + 256, physical));
    ASTIA_CHECK(physical == 6);
}

ASTIA_TEST(ext4_reads_multi_block_extent_and_sparse_hole) {
    image image_data;
    image_data.put32(4480 + 0x04, 2 * 1024);
    image_data.put16(4480 + 0x38, 2);
    const char second[] = "second extent block\n";
    std::memcpy(image_data.bytes.data() + 7 * 1024, second, sizeof second - 1);
    astia::ext4::reader reader;
    astia::ext4::inode file;
    ASTIA_CHECK(reader.open(read_image, &image_data));
    ASTIA_CHECK(reader.read_inode(12, file));
    char contents[32]{};
    ASTIA_CHECK(reader.read_file(file, 1024, contents, sizeof contents));
    ASTIA_CHECK(std::strcmp(contents, "second extent block\n") == 0);

    image_data.put16(4480 + 0x38, 32769);
    ASTIA_CHECK(reader.open(read_image, &image_data));
    ASTIA_CHECK(reader.read_inode(12, file));
    char hole[32];
    for (u32 i = 0; i < sizeof hole; ++i)
        hole[i] = static_cast<char>(0xA5);
    ASTIA_CHECK(reader.read_file(file, 0, hole, sizeof hole));
    for (u32 i = 0; i < sizeof hole; ++i)
        ASTIA_CHECK(hole[i] == 0);
}

ASTIA_TEST(ext4_validates_extent_tree_bounds_and_checksums) {
    image image_data;
    make_extent_checksum_file(image_data);
    astia::ext4::reader reader;
    astia::ext4::inode file;
    ASTIA_CHECK(reader.open(read_image, &image_data));
    ASTIA_CHECK(reader.read_inode(12, file));
    char contents[32]{ };
    ASTIA_CHECK(reader.read_file(file, 0, contents, sizeof contents));
    ASTIA_CHECK(std::strcmp(contents, "extent checksum\n") == 0);

    image_data.bytes[8 * 1024 + 1020] ^= 1;
    ASTIA_CHECK(!reader.read_file(file, 0, contents, sizeof contents));
    ASTIA_CHECK(reader.last_error() == astia::ext4::error::checksum);

    image_data = image{ };
    make_extent_checksum_file(image_data);
    image_data.put16(8 * 1024 + 4, 85);
    update_extent_checksum(image_data);
    ASTIA_CHECK(reader.open(read_image, &image_data));
    ASTIA_CHECK(reader.read_inode(12, file));
    ASTIA_CHECK(!reader.read_file(file, 0, contents, sizeof contents));
    ASTIA_CHECK(reader.last_error() == astia::ext4::error::corrupt);

    image_data = image{ };
    make_extent_checksum_file(image_data);
    image_data.put32(8 * 1024 + 20, 0);
    update_extent_checksum(image_data);
    ASTIA_CHECK(reader.open(read_image, &image_data));
    ASTIA_CHECK(reader.read_inode(12, file));
    ASTIA_CHECK(!reader.read_file(file, 0, contents, sizeof contents));
    ASTIA_CHECK(reader.last_error() == astia::ext4::error::corrupt);
}

ASTIA_TEST(ext4_rejects_magic_feature_and_checksum_errors) {
    image image_data;
    astia::ext4::reader reader;
    image_data.put16(1024 + 0x38, 0);
    ASTIA_CHECK(!reader.open(read_image, &image_data));
    ASTIA_CHECK(reader.last_error() == astia::ext4::error::bad_magic);

    image_data = image{};
    image_data.put32(1024 + 0x5C, 0x0004);
    ASTIA_CHECK(!reader.open(read_image, &image_data));
    ASTIA_CHECK(reader.last_error() == astia::ext4::error::unsupported_feature);

    image_data = image{};
    make_checksum_superblock(image_data);
    ASTIA_CHECK(reader.open(read_image, &image_data));
    image_data.bytes[1024 + 0x3FC] ^= 1;
    ASTIA_CHECK(!reader.open(read_image, &image_data));
    ASTIA_CHECK(reader.last_error() == astia::ext4::error::checksum);
}

ASTIA_TEST(ext4_reads_indexed_directory_and_rejects_bad_index) {
    image image_data;
    make_indexed_directory(image_data);
    astia::ext4::reader reader;
    astia::ext4::inode root;
    ASTIA_CHECK(reader.open(read_image, &image_data));
    ASTIA_CHECK(reader.read_inode(2, root));
    directory_state state;
    ASTIA_CHECK(reader.read_directory(root, find_known, &state));
    ASTIA_CHECK(state.found && state.inode == 12);

    image_data.bytes[6 * 1024 + 8] = 0;
    ASTIA_CHECK(!reader.read_directory(root, find_known, &state));
    ASTIA_CHECK(reader.last_error() == astia::ext4::error::corrupt);

    image_data = image{};
    make_indexed_directory(image_data);
    image_data.put32(1024 + 0x60, 0x4042);
    ASTIA_CHECK(reader.open(read_image, &image_data));
}

ASTIA_TEST(ext4_validates_indexed_directory_checksums) {
    image image_data;
    make_indexed_checksum_directory(image_data);
    astia::ext4::reader reader;
    astia::ext4::inode root;
    ASTIA_CHECK(reader.open(read_image, &image_data));
    ASTIA_CHECK(reader.read_inode(2, root));
    directory_state state;
    ASTIA_CHECK(reader.read_directory(root, find_known, &state));
    ASTIA_CHECK(state.found && state.inode == 12);

    image_data.bytes[5 * 1024 + 1019] ^= 1;
    ASTIA_CHECK(!reader.read_directory(root, find_known, &state));
    ASTIA_CHECK(reader.last_error() == astia::ext4::error::checksum);
}
