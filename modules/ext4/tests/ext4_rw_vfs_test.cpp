#include "arena.hpp"

#include <astia/fs.hpp>
#include <astia/fs/module.hpp>
#include <astia/log.hpp>
#include <astia/mm/frame_allocator.hpp>
#include <astia/mm/heap.hpp>

#include <astia/dev/blk/blkdev.hpp>
#include <astia/dev/device_file.hpp>

#include "ext4.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

alignas(16) unsigned char ext4_rw_heap_storage[1024 * 1024];

namespace astia::proc {
    mm::shptr<fs::open_file> controlling_terminal() {
        return {};
    }
}

namespace {
    using astia::u16;
    using astia::u32;
    using astia::u64;
    using astia::u8;
    using astia::usz;

    constexpr u32 sector_size    = 512;
    constexpr u32 payload_size   = 100000;

    u8 payload[payload_size];
    u8 readback[payload_size];

    u8 pattern_byte(u32 index) {
        return static_cast<u8>((index * 37u + 11u) & 0xFFu);
    }

    bool check(bool condition, const char* description) {
        if (!condition)
            std::printf("FAIL: %s\n", description);

        return condition;
    }

    template <typename T>
    bool check_handle(const astia::mm::shptr<T>& handle, const char* description) {
        return check(handle.get() != nullptr, description);
    }

    class fake_blkdev final : public astia::dev::blk::blkdev {
        public:
        fake_blkdev() : blkdev("fake-ext4", 0, sector_size) {}

        ~fake_blkdev() {
            std::free(image);
        }

        bool load(const char* path) {
            std::FILE* file = std::fopen(path, "rb");

            if (file == nullptr)
                return false;

            if (std::fseek(file, 0, SEEK_END) != 0) {
                std::fclose(file);

                return false;
            }

            const long end = std::ftell(file);

            if (end <= 0 || std::fseek(file, 0, SEEK_SET) != 0) {
                std::fclose(file);

                return false;
            }

            const usz size = static_cast<usz>(end);

            u8* loaded = static_cast<u8*>(std::realloc(image, size));

            if (loaded == nullptr) {
                std::fclose(file);

                return false;
            }

            image = loaded;

            if (std::fread(image, 1, size, file) != size) {
                std::fclose(file);

                return false;
            }

            std::fclose(file);

            _size = size;
            configure_geometry(size, sector_size);

            return true;
        }

        bool store(const char* path) const {
            std::FILE* file = std::fopen(path, "wb");

            if (file == nullptr)
                return false;

            const bool written =
                std::fwrite(image, 1, static_cast<usz>(_size), file) == _size;

            return std::fclose(file) == 0 && written;
        }

        u64 size() const {
            return _size;
        }

        protected:
        long read_blocks(void* buffer, u32 length, u64 offset) override {
            if (offset > _size || length > _size - offset)
                return -1;

            std::memcpy(buffer, image + offset, length);

            return length;
        }

        long write_blocks(const void* buffer, u32 length, u64 offset) override {
            if (offset > _size || length > _size - offset)
                return -1;

            std::memcpy(image + offset, buffer, length);

            return length;
        }

        private:
        u8* image = nullptr;
        u64 _size = 0;
    };

    fake_blkdev             fake_device;
    astia::dev::device_file fake_file(&fake_device);

    bool image_read(void*, u64 offset, void* buffer, usz length) {
        if (length > 0xFFFFFFFFu)
            return false;

        return fake_device.read(buffer, static_cast<u32>(length), offset) ==
               static_cast<long>(length);
    }

    bool image_write(void*, u64 offset, const void* buffer, usz length) {
        if (length > 0xFFFFFFFFu)
            return false;

        return fake_device.write(buffer, static_cast<u32>(length), offset) ==
               static_cast<long>(length);
    }

    bool open_volume(astia::ext4::volume& v, bool writable) {
        astia::ext4::device_io io{image_read, image_write, nullptr,
                                  fake_device.size(), sector_size};

        return v.open(io, writable);
    }

    bool make_orphan_list(astia::ext4::volume& v, u32 inode_number,
                          u32 next_number) {
        if (next_number != 0) {
            astia::ext4::inode_record following;

            if (!v.read_inode(next_number, following))
                return false;

            following.set_dtime(0);
            following.set_links_count(0);

            if (!v.write_inode(following))
                return false;
        }

        astia::ext4::inode_record node;

        if (!v.read_inode(inode_number, node))
            return false;

        node.set_dtime(next_number);
        node.set_links_count(0);

        if (!v.write_inode(node))
            return false;

        v.sb().set_last_orphan(inode_number);

        return v.write_superblock();
    }

    bool make_orphan_file(astia::ext4::volume& v, u32 inode_number) {
        const u32 file_inode = v.sb().orphan_file_inum();

        if (file_inode == 0)
            return false;

        astia::ext4::inode_record file;

        if (!v.read_inode(file_inode, file))
            return false;

        u64 physical = 0;

        if (!astia::ext4::extent::map(v, file, 0, physical))
            return false;

        const u32 block_size = v.block_size();
        u8        block[astia::ext4::sector_size_max];

        if (!v.read_block(physical, block))
            return false;

        astia::ext4::store32(block, inode_number);
        astia::ext4::store32(block + block_size - 8,
                             astia::ext4::orphan_file_magic);
        astia::ext4::store32(block + block_size - 4, 0);
        astia::ext4::store32(
            block + block_size - 4,
            astia::ext4::orphan_block_checksum(
                v.checksum_seed(), file_inode, file.generation(), physical,
                block, block_size));

        if (!v.write_block(physical, block))
            return false;

        astia::ext4::inode_record victim;

        if (!v.read_inode(inode_number, victim))
            return false;

        victim.set_dtime(0);
        victim.set_links_count(0);

        if (!v.write_inode(victim))
            return false;

        v.sb().set_feature_ro_compat(v.sb().feature_ro_compat() |
                                     astia::ext4::ro_orphan_present);

        return v.write_superblock();
    }

    bool corrupt_orphan_file(astia::ext4::volume& v) {
        const u32 file_inode = v.sb().orphan_file_inum();

        if (file_inode == 0)
            return false;

        astia::ext4::inode_record file;

        if (!v.read_inode(file_inode, file))
            return false;

        u64 physical = 0;

        if (!astia::ext4::extent::map(v, file, 0, physical))
            return false;

        const u32 block_size = v.block_size();
        u8        block[astia::ext4::sector_size_max];

        if (!v.read_block(physical, block))
            return false;

        block[block_size - 4] ^= 0xFF;

        return v.write_block(physical, block);
    }

    bool no_mapping(u64, u64) {
        return true;
    }

    bool initialize_runtime() {
        astia_test::map_arena();

        astia::mm::memory_map map;

        map.add(astia_test::arena_base, astia_test::arena_size,
                astia::mm::memory_type::usable);
        astia::mm::kernel_frames().init(map);
        astia::mm::heap::configure(ext4_rw_heap_storage,
                                   sizeof ext4_rw_heap_storage, no_mapping);

        if (!check(astia::mm::heap::init(), "heap init") ||
            !check(astia::fs::init(), "fs init"))
            return false;

        if (!check(astia::fs::register_device(
                       "/dev/fake-ext4",
                       astia::mm::shptr<astia::fs::file>::borrowed(&fake_file)) == 0,
                   "register fake device"))
            return false;

        if (!check(astia::fs::make_directory(
                       "/tmp/ext4",
                       astia::fs::file_mode(astia::fs::directory_dir,
                                            astia::fs::file_permissions(0755))) == 0,
                   "create mount point"))
            return false;

        if (!check(astia::ext4::register_filesystem(), "register ext4"))
            return false;

        return check(astia::fs::mount_type("ext4", "/tmp/ext4", "/dev/fake-ext4") == 0,
                     "mount ext4") &&
               check(!astia::fs::read_only_path("/tmp/ext4"), "mount is writable");
    }

    bool verify_payload_prefix(const u8* data, u32 length) {
        for (u32 i = 0; i < length; ++i) {
            if (data[i] != pattern_byte(i))
                return false;
        }

        return true;
    }
} // namespace

int main(int argc, char** argv) {
    const char* input = argc > 1 ? argv[1] : std::getenv("EXT4_RW_IMAGE");
    const char* output = argc > 2 ? argv[2] : std::getenv("EXT4_RW_OUTPUT");
    const char* mode   = argc > 3 ? argv[3] : std::getenv("EXT4_RW_MODE");

    if (input == nullptr || output == nullptr) {
        std::printf("usage: %s INPUT OUTPUT [MODE]\n", argv[0]);

        return 2;
    }

    if (mode == nullptr)
        mode = "normal";

    if (!check(fake_device.load(input), "load input image"))
        return 1;

    if (std::strcmp(mode, "orphan-make-list") == 0 ||
        std::strcmp(mode, "orphan-make-chain") == 0 ||
        std::strcmp(mode, "orphan-make-file") == 0) {
        const bool chain = std::strcmp(mode, "orphan-make-chain") == 0;

        if (argc < (chain ? 6 : 5)) {
            std::printf("usage: %s INPUT OUTPUT %s INODE%s\n", argv[0], mode,
                        chain ? " NEXT" : "");

            return 2;
        }

        const u32 inode_number =
            static_cast<u32>(std::strtoul(argv[4], nullptr, 10));
        const u32 next_number =
            chain ? static_cast<u32>(std::strtoul(argv[5], nullptr, 10)) : 0;
        astia::ext4::volume volume;

        if (!check(inode_number != 0 && open_volume(volume, true),
                   "open volume for orphan fixture"))
            return 1;

        bool built = false;

        if (chain)
            built = next_number != 0 &&
                    make_orphan_list(volume, inode_number, next_number);
        else if (std::strcmp(mode, "orphan-make-list") == 0)
            built = make_orphan_list(volume, inode_number, 0);
        else
            built = make_orphan_file(volume, inode_number);

        if (!check(built, "build orphan fixture") ||
            !check(fake_device.store(output), "store orphan fixture"))
            return 1;

        std::printf("OK\n");

        return 0;
    }

    if (std::strcmp(mode, "orphan-corrupt-file") == 0) {
        astia::ext4::volume volume;

        if (!check(open_volume(volume, true),
                   "open volume for orphan corruption") ||
            !check(corrupt_orphan_file(volume), "corrupt orphan file") ||
            !check(fake_device.store(output), "store corrupted orphan fixture"))
            return 1;

        std::printf("OK\n");

        return 0;
    }

    if (std::strcmp(mode, "orphan-mount") == 0) {
        if (!initialize_runtime())
            return 1;

        if (!check(astia::fs::unmount_type("/tmp/ext4") == 0,
                   "unmount orphan-cleaned ext4") ||
            !check(fake_device.store(output), "store orphan-cleaned image"))
            return 1;

        std::printf("OK\n");

        return 0;
    }

    if (!initialize_runtime())
        return 1;

    for (u32 i = 0; i < payload_size; ++i) {
        payload[i] = pattern_byte(i);
    }

    if (std::strcmp(mode, "crash") == 0 ||
        std::strcmp(mode, "crash-checkpoint") == 0) {
        if (!check(astia::fs::make_directory(
                       "/tmp/ext4/crash",
                       astia::fs::file_mode(astia::fs::directory_dir,
                                            astia::fs::file_permissions(0755))) == 0,
                   "mkdir crash"))
            return 1;

        auto keep = astia::fs::open("/tmp/ext4/crash/keep.txt",
                                    ASTIA_AIO_O_CREAT | ASTIA_AIO_O_RDWR);

        const u8 seed = 0xA5;

        if (!check_handle(keep, "create crash/keep.txt") ||
            !check(keep.get() != nullptr && keep->write(&seed, 1) == 1,
                   "write crash/keep.txt seed byte"))
            return 1;

        if (std::strcmp(mode, "crash-checkpoint") == 0)
            astia::ext4::journal::test_set_crash_after_checkpoint(true);
        else
            astia::ext4::journal::test_set_crash_after_commit(true);

        u8 crash_data[8192];

        for (u32 i = 0; i < sizeof crash_data; ++i)
            crash_data[i] = pattern_byte(i);

        const long written =
            keep->write(crash_data, sizeof crash_data, 0);

        if (!check(written == static_cast<long>(sizeof crash_data) ||
                       written == -EIO,
                   "crash write attempted"))
            return 1;

        keep = {};

        if (!check(fake_device.store(output), "store crash image"))
            return 1;

        std::printf("OK\n");

        return 0;
    }

    if (std::strcmp(mode, "recover") == 0) {
        auto keep = astia::fs::open("/tmp/ext4/crash/keep.txt",
                                    ASTIA_AIO_O_RDONLY);

        if (!check_handle(keep, "open recovered crash/keep.txt") ||
            !check(keep.get() != nullptr && keep->size() == 8192,
                   "recovered keep.txt size") ||
            !check(keep.get() != nullptr &&
                       keep->read(readback, 8192) == 8192,
                   "read recovered keep.txt"))
            return 1;

        for (u32 i = 0; i < 8192; ++i) {
            if (!check(readback[i] == pattern_byte(i),
                       "recovered keep.txt pattern"))
                return 1;
        }

        keep = {};

        if (!check(astia::fs::unmount_type("/tmp/ext4") == 0,
                   "unmount recovered ext4") ||
            !check(fake_device.store(output), "store recovered image"))
            return 1;

        std::printf("OK\n");

        return 0;
    }

    if (std::strcmp(mode, "indexed") == 0) {
        if (!check(astia::fs::make_directory(
                       "/tmp/ext4/other",
                       astia::fs::file_mode(astia::fs::directory_dir,
                                            astia::fs::file_permissions(0755))) == 0,
                   "mkdir /other") ||
            !check(astia::fs::rename_path("/tmp/ext4/big",
                                          "/tmp/ext4/other/big") == 0,
                   "rename indexed /big into /other"))
            return 1;

        bool known_seen = false;

        for (u64 index = 0; index < 512; ++index) {
            char                 entry_name[256]{};
            astia::fs::file_type entry_type{};
            const long           rc = astia::fs::read_directory(
                "/tmp/ext4/other/big", index, entry_name, sizeof entry_name,
                &entry_type, nullptr);

            if (rc == 0)
                break;

            if (!check(rc > 0, "enumerate indexed /big"))
                return 1;

            if (std::strcmp(entry_name, "known.txt") == 0)
                known_seen = true;
        }

        if (!check(known_seen, "known file visible in indexed /big"))
            return 1;

        const char* known_text   = "indexed fixture known payload\n";
        const u32   known_length = static_cast<u32>(std::strlen(known_text));

        auto known = astia::fs::open("/tmp/ext4/other/big/known.txt",
                                     ASTIA_AIO_O_RDONLY);

        char known_read[64]{};

        if (!check_handle(known, "open known.txt in indexed /other/big") ||
            !check(known.get() != nullptr &&
                       known->read(known_read, sizeof known_read) ==
                           static_cast<long>(known_length),
                   "read known.txt") ||
            !check(std::strcmp(known_read, known_text) == 0,
                   "known.txt content"))
            return 1;

        known = {};

        const char* added_text   = "added through one-time deindex\n";
        const u32   added_length = static_cast<u32>(std::strlen(added_text));

        auto added = astia::fs::open("/tmp/ext4/other/big/added.txt",
                                     ASTIA_AIO_O_CREAT | ASTIA_AIO_O_WRONLY);

        if (!check_handle(added, "create /other/big/added.txt") ||
            !check(added.get() != nullptr &&
                       added->write(added_text, added_length) ==
                           static_cast<long>(added_length),
                   "write /other/big/added.txt"))
            return 1;

        added = {};

        const char* second_text   = "second insertion on linear directory\n";
        const u32   second_length = static_cast<u32>(std::strlen(second_text));

        auto second = astia::fs::open("/tmp/ext4/other/big/added2.txt",
                                      ASTIA_AIO_O_CREAT | ASTIA_AIO_O_WRONLY);

        if (!check_handle(second, "create /other/big/added2.txt") ||
            !check(second.get() != nullptr &&
                       second->write(second_text, second_length) ==
                           static_cast<long>(second_length),
                   "write /other/big/added2.txt"))
            return 1;

        second = {};

        auto verify_added = astia::fs::open("/tmp/ext4/other/big/added.txt",
                                            ASTIA_AIO_O_RDONLY);

        char added_read[64]{};

        if (!check_handle(verify_added, "reopen /other/big/added.txt") ||
            !check(verify_added.get() != nullptr &&
                       verify_added->read(added_read, sizeof added_read) ==
                           static_cast<long>(added_length),
                   "read /other/big/added.txt") ||
            !check(std::strcmp(added_read, added_text) == 0,
                   "/other/big/added.txt content"))
            return 1;

        verify_added = {};

        if (!check(astia::fs::unmount_type("/tmp/ext4") == 0,
                   "unmount indexed ext4") ||
            !check(fake_device.store(output), "store indexed image"))
            return 1;

        std::printf("OK\n");

        return 0;
    }

    auto hello = astia::fs::open("/tmp/ext4/hello",
                                 ASTIA_AIO_O_CREAT | ASTIA_AIO_O_RDWR);

    if (!check_handle(hello, "create hello") ||
        !check(hello.get() != nullptr && hello->write(payload, payload_size) ==
                            static_cast<long>(payload_size),
               "write hello payload"))
        return 1;

    const u64 hello_inode = astia::fs::node_inode("/tmp/ext4/hello");

    hello = {};

    auto reopened = astia::fs::open("/tmp/ext4/hello", ASTIA_AIO_O_RDONLY);

    if (!check_handle(reopened, "reopen hello") ||
        !check(reopened.get() != nullptr &&
                   reopened->read(readback, payload_size) ==
                       static_cast<long>(payload_size),
               "read hello payload") ||
        !check(verify_payload_prefix(readback, payload_size),
               "hello payload matches") ||
        !check(astia::fs::node_inode("/tmp/ext4/hello") == hello_inode,
               "hello inode is stable"))
        return 1;

    reopened = {};

    auto grow  = astia::fs::open("/tmp/ext4/hello", ASTIA_AIO_O_RDWR);
    auto stale = astia::fs::open("/tmp/ext4/hello", ASTIA_AIO_O_RDWR);

    if (!check_handle(grow, "open hello for growth") ||
        !check_handle(stale, "open hello second handle") ||
        !check(grow->truncate(200000) == 0,
               "grow hello through first handle"))
        return 1;

    const u8 marker = 0x5A;

    if (!check(stale->write(&marker, 1, 150000) == 1,
               "write through second handle") ||
        !check(grow->size() == 200000, "first handle sees grown size") ||
        !check(stale->size() == 200000, "second handle keeps grown size"))
        return 1;

    u8 seen = 0;

    if (!check(grow->read(&seen, 1, 150000) == 1, "read marker byte") ||
        !check(seen == marker, "marker byte landed"))
        return 1;

    grow  = {};
    stale = {};

    if (!check(astia::fs::make_directory(
                   "/tmp/ext4/sub",
                   astia::fs::file_mode(astia::fs::directory_dir,
                                        astia::fs::file_permissions(0755))) == 0,
               "mkdir sub"))
        return 1;

    auto data = astia::fs::open("/tmp/ext4/sub/data",
                                ASTIA_AIO_O_CREAT | ASTIA_AIO_O_WRONLY);

    const char* chunk = "journaled ext4 payload\n";
    const u32   chunk_length = static_cast<u32>(std::strlen(chunk));

    if (!check_handle(data, "create sub/data"))
        return 1;

    for (u32 i = 0; i < 8; ++i) {
        if (!check(data->write(chunk, chunk_length) ==
                       static_cast<long>(chunk_length),
                   "write sub/data chunk"))
            return 1;
    }

    data = {};

    if (!check(astia::fs::rename_path("/tmp/ext4/sub/data",
                                      "/tmp/ext4/sub/moved") == 0,
               "rename data to moved") ||
        !check(astia::fs::link_path("/tmp/ext4/sub/moved",
                                    "/tmp/ext4/sub/moved2") == 0,
               "hard link moved to moved2") ||
        !check(astia::fs::make_symlink("moved", "/tmp/ext4/sub/link") == 0,
               "create symlink"))
        return 1;

    char target[32]{};

    if (!check(astia::fs::read_symlink("/tmp/ext4/sub/link", target,
                                       sizeof target) == 5,
               "read symlink length") ||
        !check(std::strcmp(target, "moved") == 0, "read symlink target") ||
        !check(astia::fs::chmod_path(
                   "/tmp/ext4/sub/moved",
                   astia::fs::file_permissions(0640)) == 0,
               "chmod moved"))
        return 1;

    if (!check(astia::fs::node_mode("/tmp/ext4/sub/moved").permissions().raw() ==
                   0640,
               "moved mode is 0640"))
        return 1;

    auto linked = astia::fs::open("/tmp/ext4/sub/moved", ASTIA_AIO_O_RDONLY);

    if (!check(linked.get() != nullptr && linked->parent() &&
                   linked->parent()->nlink() == 2,
               "moved has two links"))
        return 1;

    linked = {};

    auto truncating = astia::fs::open("/tmp/ext4/hello", ASTIA_AIO_O_RDWR);

    if (!check_handle(truncating, "open hello for truncate") ||
        !check(truncating.get() != nullptr && truncating->truncate(1500) == 0,
               "truncate hello") ||
        !check(truncating.get() != nullptr && truncating->size() == 1500,
               "hello is 1500 bytes"))
        return 1;

    if (!check(truncating->read(readback, 1500) == 1500, "read truncated hello") ||
        !check(verify_payload_prefix(readback, 1500),
               "truncated hello matches prefix"))
        return 1;

    truncating = {};

    if (!check(astia::fs::remove_path("/tmp/ext4/sub/moved2", false) == 0,
               "unlink moved2") ||
        !check(astia::fs::remove_path("/tmp/ext4/sub/moved", false) == 0,
               "unlink moved") ||
        !check(astia::fs::remove_path("/tmp/ext4/sub/link", false) == 0,
               "unlink link") ||
        !check(astia::fs::remove_path("/tmp/ext4/sub", true) == 0,
               "rmdir sub") ||
        !check(astia::fs::remove_path("/tmp/ext4/hello", false) == 0,
               "unlink hello"))
        return 1;

    char root_names[20][61]{};

    for (u32 i = 0; i < 20; ++i) {
        root_names[i][0] = 'r';
        root_names[i][1] = 'o';
        root_names[i][2] = 'o';
        root_names[i][3] = 't';

        for (u32 j = 4; j < 58; ++j) {
            root_names[i][j] =
                static_cast<char>('a' + (i * 7 + j) % 26);
        }

        root_names[i][58] = static_cast<char>('0' + (i / 10) % 10);
        root_names[i][59] = static_cast<char>('0' + i % 10);
        root_names[i][60] = '\0';
    }

    for (u32 i = 0; i < 20; ++i) {
        char path[128]{};

        std::snprintf(path, sizeof path, "/tmp/ext4/%s", root_names[i]);

        auto entry = astia::fs::open(path, ASTIA_AIO_O_CREAT | ASTIA_AIO_O_WRONLY);

        if (!check_handle(entry, "create root entry") ||
            !check(astia::fs::node_type(path) == astia::fs::directory_reg,
                   "root entry resolves"))
            return 1;

        entry = {};
    }

    bool root_seen[20]{};
    u32  root_entries = 0;

    for (u64 index = 0; index < 256; ++index) {
        char                entry_name[256]{};
        astia::fs::file_type entry_type{};
        const long          rc = astia::fs::read_directory(
            "/tmp/ext4", index, entry_name, sizeof entry_name, &entry_type,
            nullptr);

        if (rc == 0)
            break;

        if (!check(rc > 0, "enumerate grown root"))
            return 1;

        ++root_entries;

        for (u32 i = 0; i < 20; ++i) {
            if (std::strcmp(entry_name, root_names[i]) == 0)
                root_seen[i] = true;
        }
    }

    if (!check(root_entries >= 20, "grown root has entries"))
        return 1;

    for (u32 i = 0; i < 20; ++i) {
        if (!check(root_seen[i], "grown root entry visible"))
            return 1;
    }

    for (u32 i = 0; i < 20; ++i) {
        char path[128]{};

        std::snprintf(path, sizeof path, "/tmp/ext4/%s", root_names[i]);

        if (!check(astia::fs::remove_path(path, false) == 0,
                   "unlink root entry"))
            return 1;
    }

    u8 victim_data[4096];

    for (u32 i = 0; i < sizeof victim_data; ++i) {
        victim_data[i] = pattern_byte(i);
    }

    auto victim = astia::fs::open("/tmp/ext4/victim",
                                  ASTIA_AIO_O_CREAT | ASTIA_AIO_O_RDWR);

    if (!check_handle(victim, "create victim") ||
        !check(victim->write(victim_data, sizeof victim_data) ==
                   static_cast<long>(sizeof victim_data),
               "write victim data"))
        return 1;

    if (!check(astia::fs::remove_path("/tmp/ext4/victim", false) == 0,
               "unlink open victim") ||
        !check(astia::fs::node_type("/tmp/ext4/victim") ==
                   astia::fs::directory_unknown,
               "victim name is gone"))
        return 1;

    u8 victim_read[4096]{};

    if (!check(victim->read(victim_read, sizeof victim_read, 0) ==
                   static_cast<long>(sizeof victim_read),
               "read unlinked open victim") ||
        !check(std::memcmp(victim_read, victim_data, sizeof victim_data) == 0,
               "unlinked victim data matches") ||
        !check(victim->size() == static_cast<long>(sizeof victim_data),
               "unlinked victim size"))
        return 1;

    victim = {};

    u32 root_links_before = 0;

    {
        auto root_handle = astia::fs::open_directory("/tmp/ext4");

        if (!check_handle(root_handle, "open root for link count") ||
            !check(root_handle->parent() &&
                       root_handle->parent()->nlink() != 0,
                   "root link count readable"))
            return 1;

        root_links_before = root_handle->parent()->nlink();
    }

    if (!check(astia::fs::make_directory(
                   "/tmp/ext4/ren1",
                   astia::fs::file_mode(astia::fs::directory_dir,
                                        astia::fs::file_permissions(0755))) == 0,
               "mkdir ren1"))
        return 1;

    u32 root_links_mkdir = 0;

    {
        auto root_handle = astia::fs::open_directory("/tmp/ext4");

        if (!check_handle(root_handle, "reopen root after mkdir") ||
            !check(root_handle->parent() &&
                       root_handle->parent()->nlink() != 0,
                   "root link count readable after mkdir"))
            return 1;

        root_links_mkdir = root_handle->parent()->nlink();
    }

    if (!check(root_links_mkdir == root_links_before + 1,
               "mkdir adds one root link") ||
        !check(astia::fs::rename_path("/tmp/ext4/ren1",
                                      "/tmp/ext4/ren2") == 0,
               "rename ren1 to ren2") ||
        !check(astia::fs::rename_path("/tmp/ext4/ren2",
                                      "/tmp/ext4/ren3") == 0,
               "rename ren2 to ren3"))
        return 1;

    u32 root_links_after = 0;

    {
        auto root_handle = astia::fs::open_directory("/tmp/ext4");

        if (!check_handle(root_handle, "reopen root for link count") ||
            !check(root_handle->parent() &&
                       root_handle->parent()->nlink() != 0,
                   "root link count readable after rename"))
            return 1;

        root_links_after = root_handle->parent()->nlink();
    }

    if (!check(root_links_after == root_links_mkdir,
               "same-parent rename keeps root link count") ||
        !check(astia::fs::node_type("/tmp/ext4/ren3") ==
                   astia::fs::directory_dir,
               "ren3 is a directory") ||
        !check(astia::fs::remove_path("/tmp/ext4/ren3", true) == 0,
               "rmdir ren3"))
        return 1;

    if (!check(astia::fs::make_directory(
                   "/tmp/ext4/result",
                   astia::fs::file_mode(astia::fs::directory_dir,
                                        astia::fs::file_permissions(0755))) == 0,
               "mkdir result"))
        return 1;

    auto persisted = astia::fs::open("/tmp/ext4/result/persisted.txt",
                                     ASTIA_AIO_O_CREAT | ASTIA_AIO_O_WRONLY);

    const char* persisted_text = "persisted through ext4 module\n";
    const u32   persisted_length =
        static_cast<u32>(std::strlen(persisted_text));

    if (!check_handle(persisted, "create persisted.txt") ||
        !check(persisted.get() != nullptr &&
                   persisted->write(persisted_text, persisted_length) ==
                                static_cast<long>(persisted_length),
               "write persisted.txt"))
        return 1;

    persisted = {};

    if (!check(astia::fs::unmount_type("/tmp/ext4") == 0, "unmount ext4") ||
        !check(astia::fs::mount_type("ext4", "/tmp/ext4", "/dev/fake-ext4") == 0,
               "remount ext4"))
        return 1;

    auto verify = astia::fs::open("/tmp/ext4/result/persisted.txt",
                                  ASTIA_AIO_O_RDONLY);

    char persisted_read[64]{};

    if (!check_handle(verify, "reopen persisted.txt") ||
        !check(verify.get() != nullptr &&
                   verify->read(persisted_read, sizeof persisted_read) ==
                             static_cast<long>(persisted_length),
               "read persisted.txt") ||
        !check(std::strcmp(persisted_read, persisted_text) == 0,
               "persisted.txt content"))
        return 1;

    verify = {};

    if (!check(astia::fs::unmount_type("/tmp/ext4") == 0, "unmount ext4 again"))
        return 1;

    if (!check(fake_device.store(output), "store output image"))
        return 1;

    std::printf("OK\n");

    return 0;
}

namespace astia::log::detail {
    void prefix(record_builder&, level, const char*, astia::u32) {}

    void emit(level, const char*, astia::u32) {}
}

namespace astia::module {
    bool make_resident() {
        return true;
    }
} // namespace astia::module

