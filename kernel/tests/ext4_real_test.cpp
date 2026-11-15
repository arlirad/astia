#include "ext4/ext4_reader.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

    using astia::u8;
    using astia::u16;
    using astia::u32;
    using astia::u64;

    struct image {
        std::vector<u8> bytes;
    };

    bool read_image(void* context, u64 offset, void* buffer, astia::usize length) {
        auto& source = *static_cast<image*>(context);
        if (offset > source.bytes.size() || length > source.bytes.size() - offset)
            return false;
        std::memcpy(buffer, source.bytes.data() + offset, length);
        return true;
    }

    struct find_state {
        const char* wanted;
        u32 inode = 0;
    };

    bool find_entry(void* context, const astia::ext4::directory_entry& entry) {
        auto& state = *static_cast<find_state*>(context);
        u32 length = 0;
        while (state.wanted[length] != '\0')
            ++length;
        if (length == entry.name_length &&
            std::memcmp(state.wanted, entry.name, length) == 0) {
            state.inode = entry.inode;
            return false;
        }
        return true;
    }

    bool find_inode(astia::ext4::reader& reader, const astia::ext4::inode& directory,
                    const char* name, astia::ext4::inode& result) {
        find_state state{name};
        if (!reader.read_directory(directory, find_entry, &state) || state.inode == 0)
            return false;
        return reader.read_inode(state.inode, result);
    }

    bool check(bool condition, const char* expression) {
        if (!condition)
            std::printf("FAIL: %s\n", expression);
        return condition;
    }

    u32 get32(const std::vector<u8>& bytes, u64 offset) {
        return static_cast<u32>(bytes[offset]) |
               static_cast<u32>(bytes[offset + 1]) << 8 |
               static_cast<u32>(bytes[offset + 2]) << 16 |
               static_cast<u32>(bytes[offset + 3]) << 24;
    }

}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::printf("usage: %s IMAGE\n", argv[0]);
        return 2;
    }
    image image_data;
    std::ifstream input(argv[1], std::ios::binary);
    image_data.bytes.assign(std::istreambuf_iterator<char>(input), {});
    if (!check(input.good() || input.eof(), "read fixture"))
        return 1;

    astia::ext4::reader reader;
    if (!check(reader.open(read_image, &image_data), "open metadata-csum fixture"))
        return 1;
    if (!check((reader.feature_ro_compat() & 0x0400) != 0,
               "metadata checksums enabled"))
        return 1;

    astia::ext4::inode root;
    astia::ext4::inode etc;
    astia::ext4::inode known;
    astia::ext4::inode directory;
    astia::ext4::inode nested;
    bool valid = check(reader.read_inode(2, root), "read root inode") &&
                 check(find_inode(reader, root, "etc", etc), "find etc") &&
                 check(find_inode(reader, etc, "known.txt", known), "find known.txt") &&
                 check(find_inode(reader, root, "dir", directory), "find dir") &&
                 check(find_inode(reader, directory, "nested.txt", nested),
                       "find nested.txt");
    char contents[64]{ };
    valid = check(valid && reader.read_file(known, 0, contents, sizeof contents),
                  "read known.txt") &&
            check(valid && std::strcmp(contents, "read-only ext4 fixture\n") == 0,
                  "known.txt contents");
    if (!valid)
        return 1;

    image corrupted = image_data;
    corrupted.bytes[1024 + 0x3FC] ^= 1;
    astia::ext4::reader damaged;
    valid = check(!damaged.open(read_image, &corrupted), "reject bad superblock checksum") &&
            check(damaged.last_error() == astia::ext4::error::checksum,
                  "superblock checksum error");

    corrupted = image_data;
    corrupted.bytes[2048 + 0x1E] ^= 1;
    valid = check(damaged.open(read_image, &corrupted), "open bad descriptor image") &&
            check(!damaged.read_inode(2, root), "reject bad descriptor checksum") &&
            check(damaged.last_error() == astia::ext4::error::checksum,
                  "descriptor checksum error") && valid;

    corrupted = image_data;
    const u64 inode_table = get32(corrupted.bytes, 2048 + 0x08);
    const u64 inode_offset = inode_table * reader.block_size() +
                             static_cast<u64>(known.number - 1) * reader.inode_size();
    corrupted.bytes[inode_offset + 0x7C] ^= 1;
    valid = check(damaged.open(read_image, &corrupted), "open bad inode image") &&
            check(!damaged.read_inode(known.number, known), "reject bad inode checksum") &&
            check(damaged.last_error() == astia::ext4::error::checksum,
                  "inode checksum error") && valid;
    return valid ? 0 : 1;
}
