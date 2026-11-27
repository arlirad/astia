#include "arena.hpp"
#include "test.hpp"

#ifdef RENAME_NOREPLACE
#undef RENAME_NOREPLACE
#endif
#ifdef RENAME_EXCHANGE
#undef RENAME_EXCHANGE
#endif

#include <astia/fs.hpp>
#include <astia/mm/frame_allocator.hpp>
#include <astia/mm/heap.hpp>

#include <cstring>

alignas(16) unsigned char ext4_test_heap_storage[256 * 1024];

#include <astia/dev/blk/blkdev.hpp>

#include "../src/dev/device_file.hpp"
#include "../src/fs/filesystem.hpp"

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

    class fake_blkdev final : public astia::dev::blk::blkdev {
        public:
        static constexpr u32 image_size = 32 * 1024;

        fake_blkdev() : blkdev("fake-ext4", image_size, 512) {
            make_valid_image();
        }

        void put16(u64 offset, u16 value) {
            _image[offset]     = static_cast<u8>(value);
            _image[offset + 1] = static_cast<u8>(value >> 8);
        }

        void put32(u64 offset, u32 value) {
            _image[offset]     = static_cast<u8>(value);
            _image[offset + 1] = static_cast<u8>(value >> 8);
            _image[offset + 2] = static_cast<u8>(value >> 16);
            _image[offset + 3] = static_cast<u8>(value >> 24);
        }

        void make_inode(u64 offset, u16 mode, u32 number, u32 block) {
            put16(offset, mode);
            put32(offset + 4, number == 12 ? 16 : 1024);
            put16(offset + 0x1A, 1);
            put32(offset + 0x20, 0x00080000);
            put32(offset + 0x64, 1);
            put16(offset + 0x28, 0xF30A);
            put16(offset + 0x2A, 1);
            put16(offset + 0x2C, 4);
            put16(offset + 0x38, 1);
            put16(offset + 0x3A, 0);
            put32(offset + 0x3C, block);
        }

        void make_symlink(u64 offset, const char* target) {
            u32 length = 0;

            while (target[length] != '\0') {
                ++length;
            }

            put16(offset, 0120777);
            put32(offset + 4, length);
            put16(offset + 0x1A, 1);
            put32(offset + 0x64, 1);
            std::memcpy(_image + offset + 0x28, target, length);
        }

        void make_long_symlink(u64 offset, u32 block, u32 length) {
            put16(offset, 0120777);
            put32(offset + 4, length);
            put16(offset + 0x1A, 1);
            put32(offset + 0x28, block);
            put32(offset + 0x64, 1);
        }

        void make_valid_image() {
            std::memset(_image, 0, sizeof _image);
            put32(1024 + 0x00, 16);
            put32(1024 + 0x04, 32);
            put32(1024 + 0x18, 0);
            put32(1024 + 0x20, 32);
            put32(1024 + 0x28, 16);
            put16(1024 + 0x38, 0xEF53);
            put32(1024 + 0x60, 0x42);
            put16(1024 + 0x58, 128);
            put16(1024 + 0xFE, 32);
            put32(2048 + 0x08, 3);
            make_inode(3200, 040755, 2, 5);
            make_inode(4480, 0100644, 12, 6);
            make_symlink(4608, "known.txt");
            make_long_symlink(4736, 7, 64);
            make_symlink(4864, "loop2");
            make_symlink(4992, "loop1");
            put32(5 * 1024, 2);
            put16(5 * 1024 + 4, 12);

            _image[5 * 1024 + 6] = 1;
            _image[5 * 1024 + 7] = 2;
            _image[5 * 1024 + 8] = '.';

            put32(5 * 1024 + 12, 2);
            put16(5 * 1024 + 16, 12);

            _image[5 * 1024 + 18] = 2;
            _image[5 * 1024 + 19] = 2;
            _image[5 * 1024 + 20] = '.';
            _image[5 * 1024 + 21] = '.';

            put32(5 * 1024 + 24, 12);
            put16(5 * 1024 + 28, 20);

            _image[5 * 1024 + 30] = 9;
            _image[5 * 1024 + 31] = 1;

            std::memcpy(_image + 5 * 1024 + 32, "known.txt", 9);
            put32(5 * 1024 + 44, 13);
            put16(5 * 1024 + 48, 20);

            _image[5 * 1024 + 50] = 5;
            _image[5 * 1024 + 51] = 7;

            std::memcpy(_image + 5 * 1024 + 52, "known", 5);
            put32(5 * 1024 + 64, 14);
            put16(5 * 1024 + 68, 20);

            _image[5 * 1024 + 70] = 4;
            _image[5 * 1024 + 71] = 7;

            std::memcpy(_image + 5 * 1024 + 72, "long", 4);
            put32(5 * 1024 + 84, 15);
            put16(5 * 1024 + 88, 20);

            _image[5 * 1024 + 90] = 5;
            _image[5 * 1024 + 91] = 7;

            std::memcpy(_image + 5 * 1024 + 92, "loop1", 5);
            put32(5 * 1024 + 104, 16);
            put16(5 * 1024 + 108, 920);

            _image[5 * 1024 + 110] = 5;
            _image[5 * 1024 + 111] = 7;

            std::memcpy(_image + 5 * 1024 + 112, "loop2", 5);
            std::memcpy(_image + 6 * 1024, "hello from ext4\n", 16);

            for (u32 i = 0; i < 64; ++i) {
                _image[7 * 1024 + i] = static_cast<u8>('a' + i % 26);
            }
        }

        void make_indexed_image() {
            make_valid_image();
            put32(1024 + 0x5C, 0x0020);
            put32(3200 + 0x04, 3 * 1024);
            put32(3200 + 0x20, 0x00081000);
            put16(3200 + 0x38, 3);
            put32(4480 + 0x3C, 8);
            std::memset(_image + 5 * 1024, 0, 3 * 1024);

            const u64 root = 5 * 1024;

            put32(root, 2);
            put16(root + 4, 12);

            _image[root + 6] = 1;
            _image[root + 7] = 2;
            _image[root + 8] = '.';

            put32(root + 12, 2);
            put16(root + 16, 1024 - 12);

            _image[root + 18]   = 2;
            _image[root + 19]   = 2;
            _image[root + 20]   = '.';
            _image[root + 21]   = '.';
            _image[root + 0x1C] = 1;
            _image[root + 0x1D] = 8;
            _image[root + 0x1E] = 1;

            put16(root + 0x20, 100);
            put16(root + 0x22, 1);
            put32(root + 0x24, 1);

            const u64 node = 6 * 1024;

            put16(node + 4, 1024);
            put16(node + 8, 100);
            put16(node + 0xA, 1);
            put32(node + 0xE, 2);

            const u64 leaf = 7 * 1024;

            put32(leaf, 12);
            put16(leaf + 4, 1024);

            _image[leaf + 6] = 9;
            _image[leaf + 7] = 1;

            std::memcpy(_image + leaf + 8, "known.txt", 9);
            std::memcpy(_image + 8 * 1024, "hello from ext4\n", 16);
        }

        void corrupt_magic() {
            put16(1024 + 0x38, 0);
        }

        void enable_journal() {
            put32(1024 + 0x5C, 0x0004);
        }

        u32 reads() const {
            return _reads;
        }

        protected:
        long read_blocks(void* buffer, u32 length, u64 offset) override {
            ++_reads;
            std::memcpy(buffer, _image + offset, length);
            return length;
        }

        private:
        u8  _image[image_size]{};
        u32 _reads = 0;
    };

    fake_blkdev             fake_device;
    astia::dev::device_file fake_file(&fake_device);
    bool                    fake_registered = false;
    bool                    fake_mountpoint = false;

    bool no_mapping(u64, u64) {
        return true;
    }

    void initialize_runtime() {
        static bool initialized = false;
        if (initialized)
            return;

        astia_test::map_arena();

        astia::mm::memory_map map;

        map.add(astia_test::arena_base, astia_test::arena_size,
                astia::mm::memory_type::usable);
        astia::mm::kernel_frames().init(map);
        astia::mm::heap::configure(ext4_test_heap_storage, sizeof ext4_test_heap_storage,
                                   no_mapping);
        ASTIA_CHECK(astia::mm::heap::init());
        ASTIA_CHECK(astia::fs::init());

        initialized = true;
    }

    void mount_fixture(fake_blkdev& device, astia::dev::device_file& file) {
        if (!fake_registered) {
            ASTIA_CHECK(astia::fs::register_device(
                            "/dev/fake-ext4",
                            astia::mm::shptr<astia::fs::file>::borrowed(&file)) == 0);

            fake_registered = true;
        }

        if (!fake_mountpoint) {
            ASTIA_CHECK(astia::fs::make_directory(
                            "/tmp/ext4", astia::fs::file_mode(
                                             astia::fs::directory_dir,
                                             astia::fs::file_permissions(0755))) == 0);

            fake_mountpoint = true;
        }

        ASTIA_CHECK(astia::fs::mount_type("ext4", "/tmp/ext4", "/dev/fake-ext4") == 0);
        ASTIA_CHECK(device.reads() != 0);
    }
} // namespace

ASTIA_TEST(ext4_vfs_mount_read_stat_readdir_and_lifetime) {
    initialize_runtime();
    mount_fixture(fake_device, fake_file);

    auto known = astia::fs::open("/tmp/ext4/known.txt");

    ASTIA_CHECK(known);
    ASTIA_CHECK(known && known->sync() == -EROFS);

    char data[32]{};

    ASTIA_CHECK(known && known->read(data, sizeof data) == 16);
    ASTIA_CHECK(std::strcmp(data, "hello from ext4\n") == 0);

    auto link = astia::fs::open("/tmp/ext4/known");

    ASTIA_CHECK(link);
    ASTIA_CHECK(link && link->read(data, sizeof data) == 16);
    ASTIA_CHECK(std::strcmp(data, "hello from ext4\n") == 0);

    char canonical[astia::fs::path_max]{};

    ASTIA_CHECK(astia::fs::resolve_path("/tmp/ext4/./known.txt", canonical,
                                        sizeof canonical) == 0);
    ASTIA_CHECK(std::strcmp(canonical, "/tmp/ext4/known.txt") == 0);
    ASTIA_CHECK(astia::fs::node_type("/tmp/ext4/known.txt") == astia::fs::directory_reg);
    ASTIA_CHECK(astia::fs::node_mode("/tmp/ext4").permissions().raw() == 0755);
    ASTIA_CHECK(astia::raw_id(astia::fs::node_uid("/tmp/ext4/known.txt")) == 0);

    char name[32]{};

    astia::fs::file_type type{};

    ASTIA_CHECK(astia::fs::read_directory("/tmp/ext4", 0, name, sizeof name, &type,
                                          nullptr) == 1);
    ASTIA_CHECK(std::strcmp(name, ".") == 0 && type == astia::fs::directory_dir);
    ASTIA_CHECK(astia::fs::read_directory("/tmp/ext4", 2, name, sizeof name, &type,
                                          nullptr) == 1);
    ASTIA_CHECK(std::strcmp(name, "known.txt") == 0 && type == astia::fs::directory_reg);
    ASTIA_CHECK(astia::fs::read_directory("/tmp/ext4", 3, name, sizeof name, &type,
                                          nullptr) == 1);
    ASTIA_CHECK(std::strcmp(name, "known") == 0 && type == astia::fs::directory_lnk);
    ASTIA_CHECK(astia::fs::read_directory("/tmp/ext4", 4, name, sizeof name, &type,
                                          nullptr) == 1);
    ASTIA_CHECK(std::strcmp(name, "long") == 0 && type == astia::fs::directory_lnk);

    char long_target[65]{};

    ASTIA_CHECK(
        astia::fs::read_symlink("/tmp/ext4/long", long_target, sizeof long_target) == 64);

    for (u32 i = 0; i < 64; ++i) {
        ASTIA_CHECK(long_target[i] == static_cast<char>('a' + i % 26));
    }

    ASTIA_CHECK(astia::fs::read_symlink("/tmp/ext4/long", long_target, 64) ==
                -ENAMETOOLONG);

    char resolved[astia::fs::path_max]{};

    ASTIA_CHECK(astia::fs::resolve_path("/tmp/ext4/loop1", resolved, sizeof resolved) ==
                -ELOOP);
    ASTIA_CHECK(!astia::fs::open("/tmp/ext4/missing"));

    ASTIA_CHECK(astia::fs::unmount_type("/tmp/ext4") == -EBUSY);

    known = {};
    link  = {};

    ASTIA_CHECK(astia::fs::unmount_type("/tmp/ext4") == 0);
}

ASTIA_TEST(devfs_mount_point_reports_a_directory_mode) {
    initialize_runtime();

    const auto mode = astia::fs::node_mode("/dev");

    ASTIA_CHECK(astia::fs::node_type("/dev") == astia::fs::directory_dir);
    ASTIA_CHECK(mode.is_directory());
    ASTIA_CHECK(mode.other_allows(astia::fs::permission_bits::execute));
}

ASTIA_TEST(devfs_unregister_and_reprobe_replaces_node) {
    initialize_runtime();
    mount_fixture(fake_device, fake_file);

    auto registered = astia::mm::shptr<astia::fs::file>::borrowed(&fake_file);
    auto old        = astia::fs::open("/dev/fake-ext4");

    ASTIA_CHECK(old);
    ASTIA_CHECK(astia::fs::unregister_device("/dev/fake-ext4", registered) == 0);

    fake_registered = false;

    ASTIA_CHECK(astia::fs::unregister_device("/dev/fake-ext4", registered) == -ENOENT);
    ASTIA_CHECK(!astia::fs::node_mode("/dev/fake-ext4").exists());
    ASTIA_CHECK(astia::fs::register_device("/dev/fake-ext4", registered) == 0);

    fake_registered = true;

    auto current = astia::fs::open("/dev/fake-ext4");

    ASTIA_CHECK(current);

    char data[512]{};

    ASTIA_CHECK(old && old->read(data, sizeof data) == 512);
    ASTIA_CHECK(current && current->read(data, sizeof data) == 512);
    ASTIA_CHECK(astia::fs::unmount_type("/tmp/ext4") == 0);
}

ASTIA_TEST(ext4_vfs_rejects_bad_images_and_mutations) {
    initialize_runtime();
    mount_fixture(fake_device, fake_file);

    auto known = astia::fs::open("/tmp/ext4/known.txt");

    ASTIA_CHECK(known && known->write("x", 1) == -EROFS);
    ASTIA_CHECK(astia::fs::remove_path("/tmp/ext4/known.txt", false) == -EROFS);

    known = {};

    ASTIA_CHECK(astia::fs::unmount_type("/tmp/ext4") == 0);

    fake_device.corrupt_magic();
    ASTIA_CHECK(astia::fs::mount_type("ext4", "/tmp/ext4", "/dev/fake-ext4") < 0);
    fake_device.make_valid_image();
    fake_device.enable_journal();
    ASTIA_CHECK(astia::fs::mount_type("ext4", "/tmp/ext4", "/dev/fake-ext4") < 0);
}

ASTIA_TEST(ext4_vfs_reads_indexed_directory) {
    initialize_runtime();
    fake_device.make_indexed_image();
    mount_fixture(fake_device, fake_file);

    auto known = astia::fs::open("/tmp/ext4/known.txt");

    ASTIA_CHECK(known);

    char data[32]{};

    ASTIA_CHECK(known && known->read(data, sizeof data) == 16);
    ASTIA_CHECK(std::strcmp(data, "hello from ext4\n") == 0);

    char name[32]{};

    astia::fs::file_type type{};

    ASTIA_CHECK(astia::fs::read_directory("/tmp/ext4", 2, name, sizeof name, &type,
                                          nullptr) == 1);
    ASTIA_CHECK(std::strcmp(name, "known.txt") == 0 && type == astia::fs::directory_reg);

    known = {};

    ASTIA_CHECK(astia::fs::unmount_type("/tmp/ext4") == 0);
    fake_device.make_valid_image();
}
