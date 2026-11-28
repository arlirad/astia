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

#include <atomic>
#include <thread>

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

    astia::fs::creation_attributes attributes(astia::u16 permissions, astia::uid_t uid,
                                              astia::gid_t gid) {
        return {astia::fs::file_permissions{permissions}, uid, gid};
    }
} // namespace

ASTIA_TEST(tmpfs_create_publishes_final_permissions_and_owner) {
    initialize_runtime();

    astia::fs::filesystems::tmpfs_mount mount;

    const astia::fs::creation_attributes private_file =
        attributes(0600, astia::uid_t{1000}, astia::gid_t{1000});

    auto secret = mount.open_creating("/secret", ASTIA_AIO_O_CREAT | ASTIA_AIO_O_WRONLY,
                                      private_file);

    ASTIA_CHECK(secret);
    ASTIA_CHECK(secret->mode().permissions().raw() == 0600);
    ASTIA_CHECK(secret->parent()->uid() == astia::uid_t{1000});
    ASTIA_CHECK(secret->parent()->gid() == astia::gid_t{1000});
    ASTIA_CHECK(mount.node_mode("/secret").permissions().raw() == 0600);
    ASTIA_CHECK(mount.node_uid("/secret") == astia::uid_t{1000});
    ASTIA_CHECK(mount.node_gid("/secret") == astia::gid_t{1000});

    const astia::fs::creation_attributes shared_file =
        attributes(0640, astia::uid_t{2000}, astia::gid_t{2000});

    auto shared = mount.open_creating("/shared", ASTIA_AIO_O_CREAT | ASTIA_AIO_O_WRONLY,
                                      shared_file);

    ASTIA_CHECK(shared);
    ASTIA_CHECK(shared->mode().permissions().raw() == 0640);
    ASTIA_CHECK(shared->parent()->uid() == astia::uid_t{2000});
    ASTIA_CHECK(shared->parent()->gid() == astia::gid_t{2000});
    ASTIA_CHECK(mount.node_mode("/shared").permissions().raw() == 0640);
    ASTIA_CHECK(mount.node_uid("/shared") == astia::uid_t{2000});
    ASTIA_CHECK(mount.node_gid("/shared") == astia::gid_t{2000});
}

ASTIA_TEST(tmpfs_create_observer_sees_final_permissions_immediately) {
    initialize_runtime();

    astia::fs::filesystems::tmpfs_mount mount;

    std::atomic<bool>       started{false};
    std::atomic<bool>       stop{false};
    std::atomic<bool>       observed{false};
    std::atomic<astia::u16> observed_mode{0xffff};

    std::thread observer([&] {
        started.store(true, std::memory_order_release);

        while (!stop.load(std::memory_order_acquire)) {
            if (mount.node_type("/observed") != astia::fs::directory_reg)
                continue;

            observed_mode.store(mount.node_mode("/observed").permissions().raw(),
                                std::memory_order_release);
            observed.store(true, std::memory_order_release);

            return;
        }
    });

    while (!started.load(std::memory_order_acquire)) {
    }

    auto file =
        mount.open_creating("/observed", ASTIA_AIO_O_CREAT | ASTIA_AIO_O_WRONLY,
                            attributes(0600, astia::uid_t{1000}, astia::gid_t{1000}));

    const bool created = static_cast<bool>(file);

    ASTIA_CHECK(created);

    stop.store(true, std::memory_order_release);

    if (created) {
        while (!observed.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }

    observer.join();

    ASTIA_CHECK(observed.load(std::memory_order_acquire));
    ASTIA_CHECK(observed_mode.load(std::memory_order_acquire) == 0600);
}

ASTIA_TEST(tmpfs_create_excl_leaves_existing_entry_untouched) {
    initialize_runtime();

    astia::fs::filesystems::tmpfs_mount mount;

    auto first =
        mount.open_creating("/exclusive", ASTIA_AIO_O_CREAT | ASTIA_AIO_O_EXCL,
                            attributes(0600, astia::uid_t{1000}, astia::gid_t{1000}));

    ASTIA_CHECK(first);

    long error = 0;

    auto second = mount.open_creating(
        "/exclusive", ASTIA_AIO_O_CREAT | ASTIA_AIO_O_EXCL,
        attributes(0666, astia::uid_t{2000}, astia::gid_t{2000}), &error);

    ASTIA_CHECK(!second);
    ASTIA_CHECK(error == -EEXIST);
    ASTIA_CHECK(mount.node_mode("/exclusive").permissions().raw() == 0600);
    ASTIA_CHECK(mount.node_uid("/exclusive") == astia::uid_t{1000});
    ASTIA_CHECK(mount.node_gid("/exclusive") == astia::gid_t{1000});
}

ASTIA_TEST(tmpfs_create_failure_rolls_back_namespace_entry) {
    initialize_runtime();

    astia::fs::filesystems::tmpfs_mount mount{2};

    ASTIA_CHECK(mount.open_creating("/one", ASTIA_AIO_O_CREAT,
                                    attributes(0600, astia::uid_t{}, astia::gid_t{})));

    ASTIA_CHECK(mount.open_creating("/two", ASTIA_AIO_O_CREAT,
                                    attributes(0600, astia::uid_t{}, astia::gid_t{})));

    long error = 0;

    auto rejected =
        mount.open_creating("/three", ASTIA_AIO_O_CREAT,
                            attributes(0600, astia::uid_t{}, astia::gid_t{}), &error);

    ASTIA_CHECK(!rejected);
    ASTIA_CHECK(error == -ENOSPC);
    ASTIA_CHECK(mount.node_type("/three") == astia::fs::directory_unknown);

    ASTIA_CHECK(mount.unlink("/one", false) == 0);

    auto replacement = mount.open_creating(
        "/three", ASTIA_AIO_O_CREAT, attributes(0600, astia::uid_t{}, astia::gid_t{}));

    ASTIA_CHECK(replacement);
    ASTIA_CHECK(mount.node_type("/three") == astia::fs::directory_reg);
}
