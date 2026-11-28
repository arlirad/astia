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

#include "../src/fs/filesystems/tmpfs/tmpfs.hpp"

namespace astia::proc {
    mm::shptr<fs::open_file> controlling_terminal() {
        return {};
    }
} // namespace astia::proc

namespace {
    alignas(16) astia::u8 tmpfs_test_heap_storage[256 * 1024];

    bool no_mapping(astia::u64, astia::u64) {
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
        astia::mm::heap::configure(tmpfs_test_heap_storage,
                                   sizeof tmpfs_test_heap_storage, no_mapping);
        ASTIA_CHECK(astia::mm::heap::init());

        initialized = true;
    }

    astia::fs::file_mode directory_mode() {
        return astia::fs::file_mode(astia::fs::directory_dir,
                                    astia::fs::default_directory_permissions);
    }
} // namespace

ASTIA_TEST(tmpfs_ownership_follows_inode_across_hard_links) {
    initialize_runtime();

    astia::fs::filesystems::tmpfs_mount mount;

    auto file = mount.open("/owned", ASTIA_AIO_O_CREAT | ASTIA_AIO_O_RDWR);

    ASTIA_CHECK(static_cast<bool>(file));

    ASTIA_CHECK(mount.chown("/owned", astia::uid_t{1000}, astia::gid_t{1000}) == 0);
    ASTIA_CHECK(mount.node_uid("/owned") == astia::uid_t{1000});
    ASTIA_CHECK(mount.node_gid("/owned") == astia::gid_t{1000});

    ASTIA_CHECK(mount.link("/owned", "/alias") == 0);
    ASTIA_CHECK(mount.node_inode("/alias") == mount.node_inode("/owned"));
    ASTIA_CHECK(mount.node_uid("/alias") == astia::uid_t{1000});

    ASTIA_CHECK(mount.chown("/alias", astia::uid_t{2000}, astia::gid_t{2000}) == 0);
    ASTIA_CHECK(mount.node_uid("/owned") == astia::uid_t{2000});
    ASTIA_CHECK(mount.node_gid("/owned") == astia::gid_t{2000});
    ASTIA_CHECK(mount.node_uid("/alias") == astia::uid_t{2000});
    ASTIA_CHECK(mount.node_gid("/alias") == astia::gid_t{2000});
    ASTIA_CHECK(file->parent()->uid() == astia::uid_t{2000});
    ASTIA_CHECK(file->parent()->gid() == astia::gid_t{2000});

    ASTIA_CHECK(file->parent()->chown(astia::uid_t{3000}, astia::gid_t{3000}) == 0);
    ASTIA_CHECK(mount.node_uid("/owned") == astia::uid_t{3000});
    ASTIA_CHECK(mount.node_gid("/owned") == astia::gid_t{3000});
    ASTIA_CHECK(mount.node_uid("/alias") == astia::uid_t{3000});
    ASTIA_CHECK(mount.node_gid("/alias") == astia::gid_t{3000});
}

ASTIA_TEST(tmpfs_ownership_survives_rename) {
    initialize_runtime();

    astia::fs::filesystems::tmpfs_mount mount;

    auto file = mount.open("/before", ASTIA_AIO_O_CREAT | ASTIA_AIO_O_RDWR);

    ASTIA_CHECK(static_cast<bool>(file));

    ASTIA_CHECK(mount.chown("/before", astia::uid_t{4000}, astia::gid_t{4001}) == 0);
    ASTIA_CHECK(mount.link("/before", "/other") == 0);
    ASTIA_CHECK(mount.rename("/before", "/after") == 0);

    ASTIA_CHECK(mount.node_uid("/after") == astia::uid_t{4000});
    ASTIA_CHECK(mount.node_gid("/after") == astia::gid_t{4001});
    ASTIA_CHECK(mount.node_uid("/other") == astia::uid_t{4000});

    ASTIA_CHECK(mount.chown("/other", astia::uid_t{5000}, astia::gid_t{5001}) == 0);
    ASTIA_CHECK(mount.node_uid("/after") == astia::uid_t{5000});
    ASTIA_CHECK(mount.node_gid("/after") == astia::gid_t{5001});
    ASTIA_CHECK(mount.node_uid("/other") == astia::uid_t{5000});
}

ASTIA_TEST(tmpfs_ownership_is_per_directory_inode) {
    initialize_runtime();

    astia::fs::filesystems::tmpfs_mount mount;

    ASTIA_CHECK(mount.mkdir("/dir", directory_mode()) == 0);
    ASTIA_CHECK(mount.mkdir("/dir/child", directory_mode()) == 0);

    ASTIA_CHECK(mount.chown("/dir", astia::uid_t{6000}, astia::gid_t{6001}) == 0);
    ASTIA_CHECK(mount.node_uid("/dir") == astia::uid_t{6000});
    ASTIA_CHECK(mount.node_gid("/dir") == astia::gid_t{6001});
    ASTIA_CHECK(mount.node_uid("/dir/child") == astia::uid_t{0});
    ASTIA_CHECK(mount.node_gid("/dir/child") == astia::gid_t{0});

    ASTIA_CHECK(mount.chown("/dir/child", astia::uid_t{7000}, astia::gid_t{7001}) == 0);
    ASTIA_CHECK(mount.node_uid("/dir") == astia::uid_t{6000});
    ASTIA_CHECK(mount.node_uid("/dir/child") == astia::uid_t{7000});

    ASTIA_CHECK(mount.rename("/dir", "/renamed") == 0);
    ASTIA_CHECK(mount.node_uid("/renamed") == astia::uid_t{6000});
    ASTIA_CHECK(mount.node_gid("/renamed") == astia::gid_t{6001});
    ASTIA_CHECK(mount.node_uid("/renamed/child") == astia::uid_t{7000});
    ASTIA_CHECK(mount.node_gid("/renamed/child") == astia::gid_t{7001});
}
