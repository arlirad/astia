#include "arena.hpp"
#include "heap_exhaustion.hpp"
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

#include "../src/fs/filesystems/initrd/initrd.hpp"

#include <cstdlib>
#include <cstring>

static_assert(astia::fs::path_max == 4096, "kernel path_max must match libc PATH_MAX");

namespace astia::proc {
    mm::shptr<fs::open_file> controlling_terminal() {
        return {};
    }
} // namespace astia::proc

namespace {
    alignas(16) astia::u8 initrd_heap_storage[256 * 1024];

    bool no_mapping(astia::u64, astia::u64) {
        return true;
    }

    // Test registration itself allocates through the kernel allocator, so the
    // heap must be ready before the first ASTIA_TEST registrar runs.
    struct runtime_bootstrap {
        runtime_bootstrap() {
            astia_test::map_arena();

            astia::mm::memory_map map;

            map.add(astia_test::arena_base, astia_test::arena_size,
                    astia::mm::memory_type::usable);
            astia::mm::kernel_frames().init(map);
            astia::mm::heap::configure(initrd_heap_storage, sizeof initrd_heap_storage,
                                       no_mapping);

            if (!astia::mm::heap::init())
                std::abort();
        }
    } runtime_bootstrap_instance;
} // namespace

ASTIA_TEST(initrd_filesystem_attach_reports_allocation_failure) {
    astia_test::heap_exhaustion exhaustion;

    ASTIA_CHECK(exhaustion.acquire());

    astia::initrd::archive archive;

    ASTIA_CHECK(!astia::fs::filesystems::initrd_type().attach(archive));
}

ASTIA_TEST(initrd_open_returns_empty_when_file_allocation_fails) {
    astia::fs::filesystems::initrd_mount mount;
    const astia::u8                      data[] = {0x42};

    ASTIA_CHECK(mount.add("oom", astia::span<const astia::u8>(data, sizeof data)));

    {
        astia_test::heap_exhaustion exhaustion;

        ASTIA_CHECK(exhaustion.acquire());
        ASTIA_CHECK(!mount.open("/oom", 0));
    }

    auto handle = mount.open("/oom", 0);

    ASTIA_CHECK(handle);

    astia::u8 byte = 0;

    ASTIA_CHECK(handle && handle->read(&byte, sizeof byte) == 1);
    ASTIA_CHECK(byte == 0x42);
}

ASTIA_TEST(initrd_accepts_paths_at_path_max) {
    constexpr astia::usize name_length = astia::fs::path_max - 2;

    char name[name_length + 1]{};
    char absolute[astia::fs::path_max]{};

    for (astia::usize i = 0; i < name_length; ++i) {
        name[i] = static_cast<char>('a' + i % 26);
    }

    absolute[0] = '/';

    for (astia::usize i = 0; i < name_length; ++i) {
        absolute[i + 1] = name[i];
    }

    astia::fs::filesystems::initrd_mount mount;
    const astia::u8                      data[] = {0x5a};

    ASTIA_CHECK(mount.add(name, astia::span<const astia::u8>(data, sizeof data)));

    auto handle = mount.open(absolute, 0);

    ASTIA_CHECK(handle);
    ASTIA_CHECK(handle && handle->size() == 1);

    char listed[astia::fs::path_max]{};

    astia::fs::file_type listed_type = astia::fs::directory_unknown;

    astia::u64 listed_inode = 1;

    ASTIA_CHECK(mount.read_directory("/", 0, listed, sizeof listed, &listed_type,
                                     &listed_inode) == 1);
    ASTIA_CHECK(listed_type == astia::fs::file_type::regular);
    ASTIA_CHECK(std::strcmp(listed, name) == 0);
    ASTIA_CHECK(listed_inode == 0);
}

ASTIA_TEST(initrd_mount_is_read_only) {
    astia::fs::filesystems::initrd_mount mount;

    ASTIA_CHECK(mount.read_only());
    ASTIA_CHECK(mount.mkdir("/x", astia::fs::file_mode(
                                      astia::fs::file_type::directory,
                                      astia::fs::default_directory_permissions)) ==
                -EROFS);
    ASTIA_CHECK(mount.unlink("/x", false) == -EROFS);
    ASTIA_CHECK(mount.rename("/a", "/b") == -EROFS);
    ASTIA_CHECK(mount.link("/a", "/b") == -EROFS);
    ASTIA_CHECK(mount.symlink("/a", "/b") == -EROFS);
    ASTIA_CHECK(mount.chmod("/a", astia::fs::default_file_permissions) == -EROFS);
    ASTIA_CHECK(mount.chown("/a", astia::uid_t{0}, astia::gid_t{0}) == -EROFS);
}
