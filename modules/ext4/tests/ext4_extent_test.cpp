#include "ext4_extent.hpp"

#include <cstdio>

namespace astia::fs {
    bool realtime_now(u64& sec, u64& nsec) {
        sec  = 1700000200;
        nsec = 0;

        return true;
    }
} // namespace astia::fs

namespace {
    using astia::ext4::inode_record;
    using astia::ext4::load16;
    using astia::ext4::load32;
    using astia::u16;
    using astia::u32;
    using astia::u64;
    using astia::u8;
    using astia::usz;

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

    bool find_entry(astia::ext4::volume& volume,
                    const astia::ext4::inode_record& directory, const char* wanted,
                    u32& inode) {
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

            if (!astia::ext4::extent::map(volume, directory,
                                          static_cast<u32>(logical), physical))
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
                    name_matches(block + offset + 8, name_length, wanted,
                                 wanted_length)) {
                    inode = entry_inode;

                    return true;
                }

                offset += record_length;
            }
        }

        return false;
    }

    void fill_block(u8* block, u32 block_size, u32 logical) {
        for (u32 i = 0; i < block_size; ++i) {
            block[i] = static_cast<u8>((logical * 17u + i) & 0xFFu);
        }
    }

    bool block_matches(const u8* block, u32 block_size, u32 logical) {
        for (u32 i = 0; i < block_size; ++i) {
            if (block[i] != static_cast<u8>((logical * 17u + i) & 0xFFu))
                return false;
        }

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

    if (!check(volume.open(io, true), "open volume for writing") ||
        !check(volume.writable(), "volume is writable") ||
        !check(volume.block_size() == 1024, "block size is 1024") ||
        !check(volume.metadata_csum(), "metadata checksums enabled") ||
        !check(volume.inode_size() == 256, "inode size is 256"))
        return 1;

    inode_record root;
    inode_record etc;
    inode_record known;

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
        !check((known.flags() & astia::ext4::inode_extents) != 0,
               "known.txt uses extents"))
        return 1;

    const u64 original_blocks = known.blocks();

    astia::ext4::space space(volume);

    const u32 block_size = volume.block_size();

    auto grow = [&](u32 logical) -> bool {
        u64 physical = 0;

        if (!check(astia::ext4::extent::map_or_alloc(volume, space, known, logical,
                                                     physical),
                   "map_or_alloc"))
            return false;

        u8 block[4096];

        fill_block(block, block_size, logical);

        if (!check(volume.write_block(physical, block), "write data block"))
            return false;

        u64 repeated = 0;

        return check(astia::ext4::extent::map(volume, known, logical, repeated) &&
                         repeated == physical,
                     "map returns allocated block");
    };

    const u32 first_range[]  = {0, 1, 2, 3};
    const u32 second_range[] = {50, 51, 52, 53};
    const u32 final_range[]  = {5000, 5001};

    for (u32 i = 0; i < 4; ++i) {
        if (!grow(first_range[i]) || !grow(second_range[i]))
            return 1;
    }
    for (u32 logical = 500; logical <= 580; ++logical) {
        if (!grow(logical))
            return 1;
    }

    for (u32 logical : final_range) {
        if (!grow(logical))
            return 1;
    }

    const u64 expected_size = static_cast<u64>(5002) * block_size;

    known.set_size(expected_size);

    if (!check(volume.write_inode(known), "write grown inode") ||
        !check(space.flush(), "flush metadata") ||
        !check(known.blocks() > original_blocks, "block count grew"))
        return 1;

    const astia::ext4::device_io read_io{device_read, nullptr, &device,
                                         device.size, 512};
    astia::ext4::volume          verifier;

    if (!check(verifier.open(read_io, false), "reopen volume read-only"))
        return 1;

    inode_record reloaded;

    if (!check(verifier.read_inode(known.number, reloaded),
               "read grown inode after reopen") ||
        !check(reloaded.size() == expected_size, "size persisted"))
        return 1;

    u8 block[4096];

    auto verify_block = [&](u32 logical) -> bool {
        u64 physical = 0;

        if (!check(astia::ext4::extent::map(verifier, reloaded, logical, physical),
                   "map written block after reopen") ||
            !check(physical != 0, "written block is allocated") ||
            !check(verifier.read_block(physical, block), "read written block"))
            return false;

        return check(block_matches(block, block_size, logical),
                     "written block content persisted");
    };

    for (u32 logical : first_range) {
        if (!verify_block(logical))
            return 1;
    }

    for (u32 logical : second_range) {
        if (!verify_block(logical))
            return 1;
    }

    for (u32 logical = 500; logical <= 580; ++logical) {
        if (!verify_block(logical))
            return 1;
    }

    for (u32 logical : final_range) {
        if (!verify_block(logical))
            return 1;
    }

    const u32 holes[] = {4, 49, 499, 1000, 4999, 5002, 6000};

    for (u32 logical : holes) {
        u64 physical = 1;

        if (!check(astia::ext4::extent::map(verifier, reloaded, logical, physical),
                   "map hole") ||
            !check(physical == 0, "hole reads as unmapped"))
            return 1;
    }

    const u32 free_inodes_before = volume.sb().free_inodes_count();

    astia::ext4::group_descriptor group_before;

    if (!check(volume.read_group(0, group_before), "read group before inodes"))
        return 1;

    const u32 group_free_before = group_before.free_inodes_count();
    const u16 itable_before     = group_before.itable_unused();

    inode_record allocated[8];
    u32          previous = 0;

    for (u32 i = 0; i < 8; ++i) {
        if (!check(space.alloc_inode(astia::ext4::node_type::regular, allocated[i]),
                   "allocate inode") ||
            !check(allocated[i].number > previous, "inode number increases") ||
            !check(allocated[i].number >= volume.sb().first_ino(),
                   "inode number above first_ino") ||
            !check(allocated[i].links_count() == 1, "inode link count is one") ||
            !check(allocated[i].type() == astia::ext4::node_type::regular,
                   "inode type is regular") ||
            !check((allocated[i].flags() & astia::ext4::inode_extents) != 0,
                   "inode uses extents") ||
            !check(allocated[i].verify_checksum(volume.checksum_seed()),
                   "inode checksum valid"))
            return 1;

        u64 physical = 1;

        if (!check(astia::ext4::extent::map(volume, allocated[i], 0, physical) &&
                       physical == 0,
                   "fresh inode has no extents"))
            return 1;

        previous = allocated[i].number;
    }

    if (!check(volume.sb().free_inodes_count() == free_inodes_before - 8,
               "superblock free inode count dropped"))
        return 1;

    for (u32 i = 0; i < 8; ++i) {
        if (!check(space.free_inode(allocated[i]), "free inode"))
            return 1;
    }

    if (!check(volume.sb().free_inodes_count() == free_inodes_before,
               "superblock free inode count restored"))
        return 1;

    astia::ext4::group_descriptor group_after;

    if (!check(volume.read_group(0, group_after), "read group after inodes") ||
        !check(group_after.free_inodes_count() == group_free_before,
               "group free inode count restored") ||
        !check(group_after.itable_unused() == itable_before,
               "group itable_unused restored"))
        return 1;

    for (u32 i = 0; i < 8; ++i) {
        inode_record freed;

        if (!check(volume.read_inode(allocated[i].number, freed),
                   "read freed inode") ||
            !check(freed.links_count() == 0, "freed inode has no links") ||
            !check(freed.dtime() != 0, "freed inode has dtime"))
            return 1;
    }

    if (!check(space.flush(), "flush after inode round trip"))
        return 1;

    std::fclose(device.file);

    std::printf("OK\n");

    return 0;
}
