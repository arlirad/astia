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

static_assert(astia::fs::path_max == 4096, "kernel path_max must match libc PATH_MAX");

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

    void make_directory_path(char* path, astia::u32 length) {
        path[0] = '/';

        for (astia::u32 i = 1; i < length; ++i) {
            path[i] = static_cast<char>('a' + i % 26);
        }

        path[length] = '\0';
    }

    void child_path(char* out, const char* parent, const char* name) {
        astia::u32 at = 0;

        while (parent[at] != '\0') {
            out[at] = parent[at];

            ++at;
        }

        out[at++] = '/';

        for (astia::u32 i = 0; name[i] != '\0'; ++i) {
            out[at++] = name[i];
        }

        out[at] = '\0';
    }
} // namespace

ASTIA_TEST(tmpfs_readdir_lists_children_under_a_long_directory_path) {
    initialize_runtime();

    astia::fs::filesystems::tmpfs_mount mount;

    char parent[astia::fs::path_max]{};

    const astia::u32 parent_length = 300;

    make_directory_path(parent, parent_length);

    ASTIA_CHECK(mount.mkdir(parent, directory_mode()) == 0);

    char child[astia::fs::path_max]{};

    child_path(child, parent, "a");
    ASTIA_CHECK(mount.mkdir(child, directory_mode()) == 0);

    child_path(child, parent, "b");
    ASTIA_CHECK(mount.mkdir(child, directory_mode()) == 0);

    char name[256]{};

    astia::fs::file_type type = astia::fs::directory_unknown;

    ASTIA_CHECK(mount.read_directory(parent, 0, name, sizeof name, &type) == 1);
    ASTIA_CHECK(std::strcmp(name, "a") == 0);
    ASTIA_CHECK(type == astia::fs::directory_dir);

    ASTIA_CHECK(mount.read_directory(parent, 1, name, sizeof name, &type) == 1);
    ASTIA_CHECK(std::strcmp(name, "b") == 0);
    ASTIA_CHECK(type == astia::fs::directory_dir);

    ASTIA_CHECK(mount.read_directory(parent, 2, name, sizeof name, &type) == 0);
}

ASTIA_TEST(tmpfs_readdir_bounds_names_by_component_capacity) {
    initialize_runtime();

    astia::fs::filesystems::tmpfs_mount mount;

    ASTIA_CHECK(mount.mkdir("/d", directory_mode()) == 0);
    ASTIA_CHECK(mount.mkdir("/d/abcdefgh", directory_mode()) == 0);

    char exact[9]{};

    astia::fs::file_type type = astia::fs::directory_unknown;

    ASTIA_CHECK(mount.read_directory("/d", 0, exact, sizeof exact, &type) == 1);
    ASTIA_CHECK(std::strcmp(exact, "abcdefgh") == 0);
    ASTIA_CHECK(type == astia::fs::directory_dir);

    char guarded[12];

    std::memset(guarded, 0x7f, sizeof guarded);

    ASTIA_CHECK(mount.read_directory("/d", 0, guarded, 8, &type) == -ENAMETOOLONG);

    for (astia::u32 i = 0; i < sizeof guarded; ++i) {
        ASTIA_CHECK(static_cast<unsigned char>(guarded[i]) == 0x7f);
    }
}
