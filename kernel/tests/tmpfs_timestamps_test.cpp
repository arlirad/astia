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

#include <cstring>

namespace astia::proc {
    mm::shptr<fs::open_file> controlling_terminal() {
        return {};
    }
} // namespace astia::proc

namespace {
    alignas(16) astia::u8 tmpfs_test_heap_storage[256 * 1024];

    astia::u64 clock_sec  = 0;
    astia::u64 clock_nsec = 0;

    bool no_mapping(astia::u64, astia::u64) {
        return true;
    }

    bool fake_clock(astia::u64& sec, astia::u64& nsec) {
        sec  = clock_sec;
        nsec = clock_nsec;

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

        astia::fs::set_realtime_source(fake_clock);

        initialized = true;
    }

    void set_clock(astia::u64 sec, astia::u64 nsec) {
        clock_sec  = sec;
        clock_nsec = nsec;
    }

    astia::fs::file_mode directory_mode() {
        return astia::fs::file_mode(astia::fs::directory_dir,
                                    astia::fs::default_directory_permissions);
    }
} // namespace

ASTIA_TEST(tmpfs_timestamps_track_read_write_and_metadata_changes) {
    initialize_runtime();

    astia::fs::filesystems::tmpfs_mount mount;

    set_clock(1000, 1);

    auto file = mount.open("/timed", ASTIA_AIO_O_CREAT | ASTIA_AIO_O_RDWR);

    ASTIA_CHECK(static_cast<bool>(file));

    astia::fs::file_timestamps times = file->timestamps();

    ASTIA_CHECK(times.atime_sec == 1000 && times.atime_nsec == 1);
    ASTIA_CHECK(times.mtime_sec == 1000 && times.mtime_nsec == 1);
    ASTIA_CHECK(times.ctime_sec == 1000 && times.ctime_nsec == 1);

    set_clock(1001, 2);

    const char payload[3] = {'a', 'b', 'c'};

    ASTIA_CHECK(file->write(payload, 3) == 3);

    times = file->timestamps();

    ASTIA_CHECK(times.atime_sec == 1000 && times.atime_nsec == 1);
    ASTIA_CHECK(times.mtime_sec == 1001 && times.mtime_nsec == 2);
    ASTIA_CHECK(times.ctime_sec == 1001 && times.ctime_nsec == 2);

    set_clock(1002, 3);

    char readback[3]{};

    ASTIA_CHECK(file->seek(0, 0) == 0);
    ASTIA_CHECK(file->read(readback, 3) == 3);
    ASTIA_CHECK(std::memcmp(readback, payload, sizeof payload) == 0);

    times = file->timestamps();

    ASTIA_CHECK(times.atime_sec == 1002 && times.atime_nsec == 3);
    ASTIA_CHECK(times.mtime_sec == 1001 && times.mtime_nsec == 2);
    ASTIA_CHECK(times.ctime_sec == 1001 && times.ctime_nsec == 2);

    set_clock(1003, 0);

    ASTIA_CHECK(file->chmod(astia::fs::file_permissions{0600}) == 0);

    times = file->timestamps();

    ASTIA_CHECK(times.ctime_sec == 1003 && times.ctime_nsec == 0);
    ASTIA_CHECK(times.atime_sec == 1002 && times.mtime_sec == 1001);

    set_clock(1004, 0);

    ASTIA_CHECK(mount.chown("/timed", astia::uid_t{10}, astia::gid_t{20}) == 0);

    times = file->timestamps();

    ASTIA_CHECK(times.ctime_sec == 1004 && times.ctime_nsec == 0);

    set_clock(1005, 0);

    ASTIA_CHECK(
        file->parent()->touch(astia::fs::file_timestamps{2000, 6, 2000, 6, 0, 0}) == 0);

    times = file->timestamps();

    ASTIA_CHECK(times.atime_sec == 2000 && times.atime_nsec == 6);
    ASTIA_CHECK(times.mtime_sec == 2000 && times.mtime_nsec == 6);
    ASTIA_CHECK(times.ctime_sec == 1005 && times.ctime_nsec == 0);

    const astia::fs::file_timestamps node_times = mount.node_timestamps("/timed");

    ASTIA_CHECK(node_times.atime_sec == times.atime_sec);
    ASTIA_CHECK(node_times.mtime_sec == times.mtime_sec);
    ASTIA_CHECK(node_times.ctime_sec == times.ctime_sec);
}

ASTIA_TEST(tmpfs_timestamps_cover_directories_symlinks_and_root) {
    initialize_runtime();

    astia::fs::filesystems::tmpfs_mount mount;

    set_clock(3000, 0);

    ASTIA_CHECK(mount.mkdir("/d", directory_mode()) == 0);

    astia::fs::file_timestamps times = mount.node_timestamps("/d");

    ASTIA_CHECK(times.atime_sec == 3000 && times.atime_nsec == 0);
    ASTIA_CHECK(times.mtime_sec == 3000 && times.mtime_nsec == 0);
    ASTIA_CHECK(times.ctime_sec == 3000 && times.ctime_nsec == 0);

    set_clock(3100, 0);

    ASTIA_CHECK(mount.symlink("target", "/d/link") == 0);

    times = mount.node_timestamps("/d/link");

    ASTIA_CHECK(times.atime_sec == 3100 && times.mtime_sec == 3100 &&
                times.ctime_sec == 3100);

    set_clock(3200, 0);

    times = mount.node_timestamps("/");

    ASTIA_CHECK(times.atime_sec == 3200 && times.mtime_sec == 3200 &&
                times.ctime_sec == 3200);

    times = mount.node_timestamps("/");

    ASTIA_CHECK(times.atime_sec == 3200 && times.mtime_sec == 3200 &&
                times.ctime_sec == 3200);
}
