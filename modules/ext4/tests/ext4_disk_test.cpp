#include "ext4_disk.hpp"

#include <cstdio>

namespace {
    using astia::u16;
    using astia::u32;
    using astia::u64;
    using astia::u8;
    using astia::usz;
    using astia::ext4::load16;
    using astia::ext4::load32;

    struct host_device {
        std::FILE* file = nullptr;
        u64        size = 0;
    };

    bool device_read(void* context, u64 offset, void* buffer, usz length) {
        auto& device = *static_cast<host_device*>(context);

        if (device.file == nullptr || offset > device.size ||
            length > device.size - offset)
            return false;

        if (std::fseek(device.file, static_cast<long>(offset), SEEK_SET) != 0)
            return false;

        if (length == 0)
            return true;

        return std::fread(buffer, 1, length, device.file) == length;
    }

    bool device_write(void* context, u64 offset, const void* buffer, usz length) {
        auto& device = *static_cast<host_device*>(context);

        if (device.file == nullptr || offset > device.size ||
            length > device.size - offset)
            return false;

        if (std::fseek(device.file, static_cast<long>(offset), SEEK_SET) != 0)
            return false;

        if (length == 0)
            return true;

        if (std::fwrite(buffer, 1, length, device.file) != length)
            return false;

        return std::fflush(device.file) == 0;
    }

    bool check(bool condition, const char* description) {
        if (!condition)
            std::printf("FAIL: %s\n", description);

        return condition;
    }

    u8 text_length(const char* text) {
        u8 length = 0;

        while (text[length] != '\0') {
            ++length;
        }

        return length;
    }

    bool name_matches(const u8* name, u8 length, const char* wanted,
                      u8 wanted_length) {
        if (length != wanted_length)
            return false;

        for (u8 i = 0; i < length; ++i) {
            if (name[i] != static_cast<u8>(wanted[i]))
                return false;
        }

        return true;
    }

    bool map_logical(astia::ext4::volume& volume,
                     const astia::ext4::inode_record& node, u32 logical,
                     u64& physical) {
        if ((node.flags() & astia::ext4::inode_extents) != 0) {
            const u8* root = node.block_area();

            if (load16(root) != astia::ext4::extent_magic)
                return false;

            const u16 entries = load16(root + 2);
            const u16 depth   = load16(root + 6);

            if (depth != 0 || entries > 4)
                return false;

            for (u16 i = 0; i < entries; ++i) {
                const u8* extent = root + 12 + static_cast<u32>(i) * 12;

                const u32 first   = load32(extent);
                const u16 encoded = load16(extent + 4);
                const u32 length =
                    encoded > 32768 ? static_cast<u32>(encoded - 32768) : encoded;
                const u64 start =
                    static_cast<u64>(load16(extent + 6)) << 32 | load32(extent + 8);

                if (length == 0 || logical < first || logical - first >= length)
                    continue;

                if (encoded > 32768) {
                    physical = 0;

                    return true;
                }

                physical = start + (logical - first);

                return physical < volume.blocks_count();
            }

            physical = 0;

            return true;
        }

        if (logical >= 12)
            return false;

        physical = node.logical_block_at(logical);

        return physical == 0 || physical < volume.blocks_count();
    }

    bool find_entry(astia::ext4::volume& volume, const astia::ext4::inode_record& directory,
                    const char* wanted, u32& inode) {
        if (!directory.is_directory())
            return false;

        const u32 block_size = volume.block_size();
        const u64 size       = directory.size();

        if (size == 0 || block_size > 4096)
            return false;

        const u64 block_count = (size + block_size - 1) / block_size;

        if (block_count > 1024)
            return false;

        const u8 wanted_length = text_length(wanted);

        u8 block[4096];

        for (u64 logical = 0; logical < block_count; ++logical) {
            u64 physical = 0;

            if (!map_logical(volume, directory, static_cast<u32>(logical), physical))
                return false;

            if (physical == 0)
                continue;

            if (!volume.read_block(physical, block))
                return false;

            u32 offset = 0;

            while (offset + 8 <= block_size) {
                const u32 entry_inode   = load32(block + offset);
                const u16 record_length = load16(block + offset + 4);
                const u8  name_length   = block[offset + 6];

                if (record_length < 8 || (record_length & 3) != 0 ||
                    record_length > block_size - offset ||
                    name_length > record_length - 8)
                    return false;

                if (entry_inode == 0 && name_length == 0 && block[offset + 7] == 0xDE)
                    break;

                if (entry_inode != 0 &&
                    name_matches(block + offset + 8, name_length, wanted, wanted_length)) {
                    inode = entry_inode;

                    return true;
                }

                offset += record_length;
            }
        }

        return false;
    }

    bool reopen(host_device& device, const char* path, u64& size) {
        if (device.file != nullptr)
            std::fclose(device.file);

        device.file = std::fopen(path, "rb");

        if (device.file == nullptr)
            return false;

        if (std::fseek(device.file, 0, SEEK_END) != 0)
            return false;

        const long end = std::ftell(device.file);

        if (end < 0)
            return false;

        size        = static_cast<u64>(end);
        device.size = size;

        return true;
    }
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::printf("usage: %s IMAGE\n", argv[0]);

        return 2;
    }

    host_device device;
    device.file = std::fopen(argv[1], "r+b");

    if (device.file == nullptr) {
        std::printf("FAIL: open image\n");

        return 1;
    }

    if (std::fseek(device.file, 0, SEEK_END) != 0) {
        std::printf("FAIL: size image\n");

        return 1;
    }

    const long image_end = std::ftell(device.file);

    if (image_end < 0) {
        std::printf("FAIL: size image\n");

        return 1;
    }

    device.size = static_cast<u64>(image_end);

    astia::ext4::device_io io{device_read, device_write, &device, device.size, 512};

    astia::ext4::volume volume;

    if (!check(volume.open(io, true), "open volume for writing"))
        return 1;

    if (!check(volume.writable(), "volume is writable") ||
        !check(volume.block_size() == 1024, "block size is 1024") ||
        !check(volume.metadata_csum(), "metadata checksums enabled") ||
        !check(volume.inode_size() == 256, "inode size is 256"))
        return 1;

    if (!check(volume.sb().verify_checksum(), "superblock checksum") ||
        !check(volume.sb().magic() == astia::ext4::super_magic, "superblock magic"))
        return 1;

    const u64 blocks_count = volume.blocks_count();

    if (!check(volume.write_superblock(), "write superblock") ||
        !check(volume.read_superblock(), "read superblock back") ||
        !check(volume.sb().verify_checksum(), "superblock checksum after round-trip") ||
        !check(volume.blocks_count() == blocks_count, "blocks count preserved"))
        return 1;

    astia::ext4::group_descriptor group;

    if (!check(volume.read_group(0, group), "read group descriptor") ||
        !check(volume.write_group(0, group), "write group descriptor"))
        return 1;

    astia::ext4::inode_record root;
    astia::ext4::inode_record etc;
    astia::ext4::inode_record known;

    if (!check(volume.read_inode(2, root), "read root inode") ||
        !check(root.is_directory(), "root is a directory"))
        return 1;

    u32 etc_inode   = 0;
    u32 known_inode = 0;

    if (!check(find_entry(volume, root, "etc", etc_inode), "find /etc") ||
        !check(volume.read_inode(etc_inode, etc), "read /etc inode") ||
        !check(find_entry(volume, etc, "known.txt", known_inode),
               "find /etc/known.txt") ||
        !check(volume.read_inode(known_inode, known), "read known.txt inode") ||
        !check(known.is_regular(), "known.txt is a regular file") ||
        !check(known.verify_checksum(volume.checksum_seed()),
               "known.txt inode checksum"))
        return 1;

    const u32 stamp = 1700000123;

    known.set_atime(stamp);
    known.set_ctime(stamp);
    known.set_mtime(stamp);

    if (!check(volume.write_inode(known), "write known.txt inode"))
        return 1;

    astia::ext4::inode_record reloaded;

    if (!check(volume.read_inode(known.number, reloaded), "read written inode") ||
        !check(reloaded.verify_checksum(volume.checksum_seed()),
               "written inode checksum") ||
        !check(reloaded.atime() == stamp, "atime written") ||
        !check(reloaded.ctime() == stamp, "ctime written") ||
        !check(reloaded.mtime() == stamp, "mtime written"))
        return 1;

    const u16 expected_mounts = static_cast<u16>(volume.sb().mnt_count() + 1);

    volume.sb().set_mnt_count(expected_mounts);
    volume.sb().set_state(1);
    volume.mark_superblock_dirty();

    if (!check(volume.sync(), "sync superblock") || !check(!volume.dirty(),
                                                            "volume is clean"))
        return 1;

    u64 reopen_size = 0;

    if (!check(reopen(device, argv[1], reopen_size), "reopen image"))
        return 1;

    astia::ext4::device_io readonly_io{device_read, nullptr, &device, reopen_size, 512};
    astia::ext4::volume     readonly;

    if (!check(readonly.open(readonly_io, false), "open volume read-only") ||
        !check(!readonly.writable(), "read-only volume is not writable"))
        return 1;

    astia::ext4::inode_record verified;

    if (!check(readonly.read_inode(known.number, verified), "read inode after reopen") ||
        !check(verified.verify_checksum(readonly.checksum_seed()),
               "inode checksum after reopen") ||
        !check(verified.atime() == stamp, "atime persisted") ||
        !check(verified.ctime() == stamp, "ctime persisted") ||
        !check(verified.mtime() == stamp, "mtime persisted") ||
        !check(readonly.sb().mnt_count() == expected_mounts, "mount count persisted") ||
        !check(readonly.sb().state() == 1, "state persisted") ||
        !check(readonly.sb().verify_checksum(), "superblock checksum persisted"))
        return 1;

    std::fclose(device.file);

    std::printf("OK\n");

    return 0;
}
