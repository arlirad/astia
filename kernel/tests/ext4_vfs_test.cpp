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
#include <cstdlib>

alignas(16) unsigned char ext4_test_heap_storage[256 * 1024];

namespace {
    bool is_heap_pointer(void* value) {
        const auto address = reinterpret_cast<astia::u64>(value);
        const auto begin = reinterpret_cast<astia::u64>(ext4_test_heap_storage);
        return address >= begin && address < begin + sizeof ext4_test_heap_storage;
    }
}

void* operator new(std::size_t size) {
    if (void* value = std::malloc(size == 0 ? 1 : size))
        return value;
    std::abort();
}
void* operator new[](std::size_t size) {
    if (void* value = std::malloc(size == 0 ? 1 : size))
        return value;
    std::abort();
}
void operator delete(void* value) noexcept {
    if (is_heap_pointer(value))
        astia::mm::heap::free(value);
    else
        std::free(value);
}
void operator delete[](void* value) noexcept {
    if (is_heap_pointer(value))
        astia::mm::heap::free(value);
    else
        std::free(value);
}
void operator delete(void* value, std::size_t) noexcept {
    if (is_heap_pointer(value))
        astia::mm::heap::free(value);
    else
        std::free(value);
}
void operator delete[](void* value, std::size_t) noexcept {
    if (is_heap_pointer(value))
        astia::mm::heap::free(value);
    else
        std::free(value);
}

#include "../src/dev/device_file.hpp"
#include "../src/dev/blk/block_device.hpp"
#include "../src/fs/filesystem.hpp"

namespace astia::proc {
    mm::shptr<fs::open_file> controlling_terminal() {
        return {};
    }
}

namespace {

    using astia::u8;
    using astia::u16;
    using astia::u32;
    using astia::u64;

    class fake_block_device final : public astia::dev::blk::block_device {
        public:
        static constexpr u32 image_size = 32 * 1024;

        fake_block_device() : block_device("fake-ext4", image_size, 512) {
            make_valid_image();
        }

        void put16(u64 offset, u16 value) {
            image_[offset] = static_cast<u8>(value);
            image_[offset + 1] = static_cast<u8>(value >> 8);
        }
        void put32(u64 offset, u32 value) {
            image_[offset] = static_cast<u8>(value);
            image_[offset + 1] = static_cast<u8>(value >> 8);
            image_[offset + 2] = static_cast<u8>(value >> 16);
            image_[offset + 3] = static_cast<u8>(value >> 24);
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
        void make_valid_image() {
            std::memset(image_, 0, sizeof image_);
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
            put32(5 * 1024, 2);
            put16(5 * 1024 + 4, 12);
            image_[5 * 1024 + 6] = 1;
            image_[5 * 1024 + 7] = 2;
            image_[5 * 1024 + 8] = '.';
            put32(5 * 1024 + 12, 2);
            put16(5 * 1024 + 16, 12);
            image_[5 * 1024 + 18] = 2;
            image_[5 * 1024 + 19] = 2;
            image_[5 * 1024 + 20] = '.';
            image_[5 * 1024 + 21] = '.';
            put32(5 * 1024 + 24, 12);
            put16(5 * 1024 + 28, 1000);
            image_[5 * 1024 + 30] = 9;
            image_[5 * 1024 + 31] = 1;
            std::memcpy(image_ + 5 * 1024 + 32, "known.txt", 9);
            std::memcpy(image_ + 6 * 1024, "hello from ext4\n", 16);
        }
        void corrupt_magic() {
            put16(1024 + 0x38, 0);
        }
        void enable_journal() {
            put32(1024 + 0x5C, 0x0004);
        }

        u32 reads() const {
            return reads_;
        }

        protected:
        long read_blocks(void* buffer, u32 length, u64 offset) override {
            ++reads_;
            std::memcpy(buffer, image_ + offset, length);
            return length;
        }

        private:
        u8 image_[image_size]{};
        u32 reads_ = 0;
    };

    fake_block_device fake_device;
    astia::dev::device_file fake_file(&fake_device);
    bool fake_registered = false;
    bool fake_mountpoint = false;

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

    void mount_fixture(fake_block_device& device, astia::dev::device_file& file) {
        if (!fake_registered) {
            ASTIA_CHECK(astia::fs::register_device(
                            "/dev/fake-ext4",
                            astia::mm::shptr<astia::fs::file>::borrowed(&file)) == 0);
            fake_registered = true;
        }
        if (!fake_mountpoint) {
            ASTIA_CHECK(astia::fs::make_directory(
                            "/tmp/ext4", astia::fs::file_mode(astia::fs::directory_dir,
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
    char data[32]{};
    ASTIA_CHECK(known && known->read(data, sizeof data) == 16);
    ASTIA_CHECK(std::strcmp(data, "hello from ext4\n") == 0);
    ASTIA_CHECK(astia::fs::node_type("/tmp/ext4/known.txt") == astia::fs::directory_reg);
    ASTIA_CHECK(astia::fs::node_mode("/tmp/ext4").permissions().raw() == 0755);
    ASTIA_CHECK(astia::raw_id(astia::fs::node_uid("/tmp/ext4/known.txt")) == 0);

    char name[32]{};
    astia::fs::file_type type{};
    ASTIA_CHECK(astia::fs::read_directory("/tmp/ext4", 0, name, sizeof name, &type) == 1);
    ASTIA_CHECK(std::strcmp(name, ".") == 0 && type == astia::fs::directory_dir);
    ASTIA_CHECK(astia::fs::read_directory("/tmp/ext4", 2, name, sizeof name, &type) == 1);
    ASTIA_CHECK(std::strcmp(name, "known.txt") == 0 && type == astia::fs::directory_reg);

    ASTIA_CHECK(astia::fs::unmount_type("/tmp/ext4") == -EBUSY);
    known = {};
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
