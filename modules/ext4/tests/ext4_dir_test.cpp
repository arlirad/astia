#include "ext4_dir.hpp"
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
    using astia::ext4::dirent_view;
    using astia::ext4::inode_record;
    using astia::u16;
    using astia::u32;
    using astia::u64;
    using astia::u8;
    using astia::usz;

    constexpr u32 test_block_size = 1024;

    struct host_device {
        std::FILE* file = nullptr;
        u64        size = 0;
    };

    bool host_read(host_device& device, u64 offset, void* buffer, usz length) {
        if (device.file == nullptr || offset > device.size ||
            length > device.size - offset)
            return false;

        if (std::fseek(device.file, static_cast<long>(offset), SEEK_SET) != 0)
            return false;

        if (length == 0)
            return true;

        return std::fread(buffer, 1, length, device.file) == length;
    }

    bool host_write(host_device& device, u64 offset, const void* buffer,
                    usz length) {
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

    astia::core::task<astia::i64> device_read(void* context, void* buffer,
                                              u32 length, u64 offset) {
        auto& device = *static_cast<host_device*>(context);

        co_return host_read(device, offset, buffer, length)
                      ? static_cast<astia::i64>(length)
                      : static_cast<astia::i64>(-EIO);
    }

    astia::core::task<astia::i64> device_write(void* context, const void* buffer,
                                               u32 length, u64 offset) {
        auto& device = *static_cast<host_device*>(context);

        co_return host_write(device, offset, buffer, length)
                      ? static_cast<astia::i64>(length)
                      : static_cast<astia::i64>(-EIO);
    }

    bool check(bool condition, const char* description) {
        if (!condition)
            std::printf("FAIL: %s\n", description);

        return condition;
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

    bool write_data_blocks(astia::ext4::volume& volume,
                           astia::ext4::space& alloc, inode_record& node,
                           u32 count) {
        u8 block[4096];

        for (u32 logical = 0; logical < count; ++logical) {
            u64 physical = 0;

            if (!check(astia::ext4::extent::map_or_alloc(volume, alloc, node,
                                                         logical, physical),
                       "map_or_alloc data block"))
                return false;

            fill_block(block, test_block_size, logical);

            if (!check(volume.write_block(physical, block),
                       "write data block"))
                return false;
        }

        node.set_size(static_cast<u64>(count) * test_block_size);

        return true;
    }

    bool verify_data_blocks(astia::ext4::volume& volume,
                            const inode_record& node, u32 count) {
        u8 block[4096];

        for (u32 logical = 0; logical < count; ++logical) {
            u64 physical = 0;

            if (!check(astia::ext4::extent::map(volume, node, logical,
                                                physical),
                       "map data block") ||
                !check(physical != 0, "data block allocated") ||
                !check(volume.read_block(physical, block), "read data block") ||
                !check(block_matches(block, test_block_size, logical),
                       "data block content"))
                return false;
        }

        return true;
    }

    void build_long_name(char* name, u8 seed) {
        name[0] = static_cast<char>('a' + seed);

        for (u32 i = 1; i < 200; ++i) {
            name[i] = 'q';
        }

        name[200] = '\0';
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

    astia::ext4::device_io io{device_read, device_write, nullptr, &device,
                              device.size, 512};

    astia::ext4::volume volume;

    if (!check(volume.open(io, true), "open volume for writing") ||
        !check(volume.writable(), "volume is writable") ||
        !check(volume.block_size() == test_block_size, "block size is 1024") ||
        !check(volume.metadata_csum(), "metadata checksums enabled") ||
        !check(volume.inode_size() == 256, "inode size is 256"))
        return 1;

    inode_record root;

    if (!check(volume.read_inode(2, root), "read root inode") ||
        !check(root.is_directory(), "root is a directory"))
        return 1;

    astia::ext4::space alloc(volume);

    inode_record newdir;

    if (!check(alloc.alloc_inode(astia::ext4::node_type::directory, newdir),
               "allocate directory inode"))
        return 1;

    newdir.set_mode(040755);

    if (!check(astia::ext4::directory::initialize(volume, alloc, newdir,
                                                  newdir.number, 2) == 0,
               "initialize directory") ||
        !check(volume.write_inode(newdir), "write directory inode") ||
        !check(astia::ext4::directory::add(volume, alloc, root, "newdir",
                                           newdir.number,
                                           astia::ext4::dirent_directory) == 0,
               "add newdir to root"))
        return 1;

    root.set_links_count(static_cast<u16>(root.links_count() + 1));

    if (!check(volume.write_inode(root), "write root inode"))
        return 1;

    inode_record file;

    if (!check(alloc.alloc_inode(astia::ext4::node_type::regular, file),
               "allocate regular inode"))
        return 1;

    file.set_mode(0100644);

    if (!write_data_blocks(volume, alloc, file, 3) ||
        !check(volume.write_inode(file), "write file inode") ||
        !check(astia::ext4::directory::add(volume, alloc, newdir, "file",
                                           file.number,
                                           astia::ext4::dirent_regular) == 0,
               "add file to newdir") ||
        !check(volume.write_inode(newdir), "write newdir inode"))
        return 1;

    u32 found_inode = 0;
    u8  found_type  = 0;

    if (!check(astia::ext4::directory::find(volume, root, "newdir",
                                            found_inode, found_type) &&
                   found_inode == newdir.number,
               "find newdir from root") ||
        !check(astia::ext4::directory::find(volume, newdir, "file",
                                            found_inode, found_type) &&
                   found_inode == file.number &&
                   found_type == astia::ext4::dirent_regular,
               "find file from newdir") ||
        !verify_data_blocks(volume, file, 3))
        return 1;

    const u64 size_before = newdir.size();
    char    long_names[6][201];
    inode_record long_files[6];

    for (u32 i = 0; i < 6; ++i) {
        build_long_name(long_names[i], static_cast<u8>(i));

        if (!check(alloc.alloc_inode(astia::ext4::node_type::regular,
                                     long_files[i]),
                   "allocate long-name inode"))
            return 1;

        long_files[i].set_mode(0100644);

        if (!check(volume.write_inode(long_files[i]),
                   "write long-name inode") ||
            !check(astia::ext4::directory::add(volume, alloc, newdir,
                                               long_names[i],
                                               long_files[i].number,
                                               astia::ext4::dirent_regular) == 0,
                   "add long-name file"))
            return 1;

        if (!check(astia::ext4::directory::find(volume, newdir, long_names[i],
                                                found_inode, found_type) &&
                       found_inode == long_files[i].number,
                   "find long-name file"))
            return 1;
    }

    if (!check(newdir.size() > size_before, "directory grew a block") ||
        !check(volume.write_inode(newdir), "write grown newdir inode") ||
        !check(alloc.flush(), "flush metadata"))
        return 1;

    std::printf("PHASE1\n");
    std::fflush(stdout);

    astia::ext4::volume reopened;

    if (!check(reopened.open(io, true), "reopen volume read-write"))
        return 1;

    astia::ext4::space reopen_alloc(reopened);

    inode_record root_reopened;
    inode_record newdir_reopened;
    inode_record file_reopened;

    if (!check(reopened.read_inode(2, root_reopened), "read root after reopen") ||
        !check(astia::ext4::directory::find(reopened, root_reopened, "newdir",
                                            found_inode, found_type) &&
                   found_inode == newdir.number,
               "find newdir after reopen") ||
        !check(reopened.read_inode(found_inode, newdir_reopened),
               "read newdir after reopen") ||
        !check(astia::ext4::directory::find(reopened, newdir_reopened, "file",
                                            found_inode, found_type) &&
                   found_inode == file.number,
               "find file after reopen") ||
        !check(reopened.read_inode(found_inode, file_reopened),
               "read file after reopen") ||
        !check(newdir_reopened.size() > size_before,
               "directory size persisted"))
        return 1;

    u32 etc_inode   = 0;
    u32 known_inode = 0;

    inode_record etc;
    inode_record known;

    if (!check(astia::ext4::directory::find(reopened, root_reopened, "etc",
                                            etc_inode, found_type),
               "find etc") ||
        !check(reopened.read_inode(etc_inode, etc), "read etc inode") ||
        !check(astia::ext4::directory::find(reopened, etc, "known.txt",
                                            known_inode, found_type),
               "find known.txt") ||
        !check(reopened.read_inode(known_inode, known),
               "read known.txt inode") ||
        !write_data_blocks(reopened, reopen_alloc, known, 200) ||
        !check(reopened.write_inode(known), "write grown known.txt"))
        return 1;

    if (!check(astia::ext4::extent::truncate(reopened, reopen_alloc, known,
                                             1500),
               "truncate known.txt") ||
        !check(known.size() == 1500, "truncated size is 1500"))
        return 1;

    u8 first[1500];
    u32 copied = 0;

    for (u32 logical = 0; logical < 2; ++logical) {
        u64 physical = 0;

        if (!check(astia::ext4::extent::map(reopened, known, logical,
                                            physical),
                   "map truncated block") ||
            !check(physical != 0, "truncated block kept"))
            return 1;

        u8 block[4096];

        if (!check(reopened.read_block(physical, block),
                   "read truncated block"))
            return 1;

        const u32 amount = 1500 - copied < test_block_size
                               ? 1500 - copied
                               : test_block_size;

        for (u32 i = 0; i < amount; ++i) {
            first[copied + i] = block[i];
        }

        copied += amount;
    }

    for (u32 i = 0; i < 1500; ++i) {
        const u32 logical = i / test_block_size;
        const u32 within  = i % test_block_size;

        if (!check(first[i] ==
                       static_cast<u8>((logical * 17u + within) & 0xFFu),
                   "truncated bytes match pattern"))
            return 1;
    }

    u64 beyond = 1;

    if (!check(astia::ext4::extent::map(reopened, known, 2, beyond) &&
                   beyond == 0,
               "blocks beyond truncation freed") ||
        !check(reopened.write_inode(known), "write truncated known.txt"))
        return 1;

    u32 removed_inode = 0;

    if (!check(astia::ext4::directory::remove(reopened, reopen_alloc,
                                              newdir_reopened, "file",
                                              removed_inode) == 0 &&
                   removed_inode == file_reopened.number,
               "remove file from newdir") ||
        !check(astia::ext4::extent::free_all(reopened, reopen_alloc,
                                             file_reopened),
               "free file extents") ||
        !check(reopen_alloc.free_inode(file_reopened), "free file inode"))
        return 1;

    for (u32 i = 0; i < 6; ++i) {
        inode_record long_file;

        if (!check(reopened.read_inode(long_files[i].number, long_file),
                   "read long-name inode after reopen") ||
            !check(astia::ext4::directory::remove(reopened, reopen_alloc,
                                                  newdir_reopened,
                                                  long_names[i],
                                                  removed_inode) == 0 &&
                       removed_inode == long_file.number,
                   "remove long-name file") ||
            !check(astia::ext4::extent::free_all(reopened, reopen_alloc,
                                                 long_file),
                   "free long-name extents") ||
            !check(reopen_alloc.free_inode(long_file),
                   "free long-name inode"))
            return 1;
    }

    if (!check(astia::ext4::directory::is_empty(reopened, newdir_reopened),
               "newdir is empty") ||
        !check(astia::ext4::directory::remove(reopened, reopen_alloc,
                                              root_reopened, "newdir",
                                              removed_inode) == 0 &&
                   removed_inode == newdir_reopened.number,
               "remove newdir from root"))
        return 1;

    root_reopened.set_links_count(
        static_cast<u16>(root_reopened.links_count() - 1));

    if (!check(reopened.write_inode(root_reopened), "write root after remove") ||
        !check(astia::ext4::extent::free_all(reopened, reopen_alloc,
                                             newdir_reopened),
               "free newdir extents") ||
        !check(reopen_alloc.free_inode(newdir_reopened), "free newdir inode") ||
        !check(reopen_alloc.flush(), "flush metadata after delete"))
        return 1;

    std::printf("OK\n");

    std::fclose(device.file);

    return 0;
}
