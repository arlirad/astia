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
        const u32 seed = astia::ext4::reader::crc32c(
            ~u32{0}, image.bytes.data() + base + 0x68, 16);
        image.put32(base + 0x3FC,
                    astia::ext4::reader::crc32c(seed, image.bytes.data() + base, 0x3FC));
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
