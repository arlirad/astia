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

    ASTIA_CHECK(mount.read_directory(parent, 0, name, sizeof name, &type, nullptr) == 1);
    ASTIA_CHECK(std::strcmp(name, "a") == 0);
    ASTIA_CHECK(type == astia::fs::directory_dir);

    ASTIA_CHECK(mount.read_directory(parent, 1, name, sizeof name, &type, nullptr) == 1);
    ASTIA_CHECK(std::strcmp(name, "b") == 0);
    ASTIA_CHECK(type == astia::fs::directory_dir);

    ASTIA_CHECK(mount.read_directory(parent, 2, name, sizeof name, &type, nullptr) == 0);
}

ASTIA_TEST(tmpfs_readdir_bounds_names_by_component_capacity) {
    initialize_runtime();

    astia::fs::filesystems::tmpfs_mount mount;

    ASTIA_CHECK(mount.mkdir("/d", directory_mode()) == 0);
    ASTIA_CHECK(mount.mkdir("/d/abcdefgh", directory_mode()) == 0);

    char exact[9]{};

    astia::fs::file_type type = astia::fs::directory_unknown;

    ASTIA_CHECK(mount.read_directory("/d", 0, exact, sizeof exact, &type, nullptr) == 1);
    ASTIA_CHECK(std::strcmp(exact, "abcdefgh") == 0);
    ASTIA_CHECK(type == astia::fs::directory_dir);

    char guarded[12];

    std::memset(guarded, 0x7f, sizeof guarded);

    ASTIA_CHECK(mount.read_directory("/d", 0, guarded, 8, &type, nullptr) ==
                -ENAMETOOLONG);

    for (astia::u32 i = 0; i < sizeof guarded; ++i) {
        ASTIA_CHECK(static_cast<unsigned char>(guarded[i]) == 0x7f);
    }
}

ASTIA_TEST(tmpfs_readdir_reports_stable_inode_identity) {
    initialize_runtime();

    astia::fs::filesystems::tmpfs_mount mount;

    ASTIA_CHECK(mount.mkdir("/identity", directory_mode()) == 0);

    auto file = mount.open("/identity/file", ASTIA_AIO_O_CREAT | ASTIA_AIO_O_WRONLY);

    ASTIA_CHECK(file);

    ASTIA_CHECK(mount.link("/identity/file", "/identity/link") == 0);
    ASTIA_CHECK(mount.mkdir("/identity/nested", directory_mode()) == 0);

    char name[256]{};

    astia::fs::file_type type = astia::fs::directory_unknown;

    astia::u64 inode = 0;

    ASTIA_CHECK(mount.read_directory("/identity", 0, name, sizeof name, &type, &inode) ==
                1);
    ASTIA_CHECK(std::strcmp(name, "file") == 0);
    ASTIA_CHECK(type == astia::fs::directory_reg);
    ASTIA_CHECK(inode != 0);
    ASTIA_CHECK(inode == mount.node_inode("/identity/file"));

    astia::u64 file_inode = inode;

    ASTIA_CHECK(mount.read_directory("/identity", 1, name, sizeof name, &type, &inode) ==
                1);
    ASTIA_CHECK(std::strcmp(name, "link") == 0);
    ASTIA_CHECK(type == astia::fs::directory_reg);
    ASTIA_CHECK(inode == file_inode);
    ASTIA_CHECK(mount.node_inode("/identity/link") == file_inode);

    ASTIA_CHECK(mount.read_directory("/identity", 2, name, sizeof name, &type, &inode) ==
                1);
    ASTIA_CHECK(std::strcmp(name, "nested") == 0);
    ASTIA_CHECK(type == astia::fs::directory_dir);
    ASTIA_CHECK(inode != 0);
    ASTIA_CHECK(inode == mount.node_inode("/identity/nested"));

    astia::u64 nested_inode = inode;

    ASTIA_CHECK(mount.read_directory("/identity", 0, name, sizeof name, &type, &inode) ==
                1);
    ASTIA_CHECK(inode == file_inode);
    ASTIA_CHECK(nested_inode != file_inode);

    ASTIA_CHECK(mount.read_directory("/identity", 3, name, sizeof name, &type, &inode) ==
                0);
}

ASTIA_TEST(tmpfs_mkdir_rejects_symlink_entries_without_following) {
    initialize_runtime();

    astia::fs::filesystems::tmpfs_mount mount;

    ASTIA_CHECK(mount.mkdir("/dir", directory_mode()) == 0);
    ASTIA_CHECK(mount.symlink("/does/not/exist", "/dangling") == 0);
    ASTIA_CHECK(mount.mkdir("/dangling", directory_mode()) == -EEXIST);
    ASTIA_CHECK(mount.node_type("/dangling") == astia::fs::directory_lnk);
    ASTIA_CHECK(mount.node_type("/does/not/exist") == astia::fs::directory_unknown);

    ASTIA_CHECK(mount.symlink("/dir", "/dirlink") == 0);
    ASTIA_CHECK(mount.mkdir("/dirlink", directory_mode()) == -EEXIST);
    ASTIA_CHECK(mount.node_type("/dirlink") == astia::fs::directory_lnk);

    ASTIA_CHECK(mount.mkdir("/plain", directory_mode()) == 0);
    ASTIA_CHECK(mount.node_type("/plain") == astia::fs::directory_dir);
}

ASTIA_TEST(tmpfs_node_path_follows_a_directory_rename) {
    initialize_runtime();

    astia::fs::filesystems::tmpfs_mount mount;

    ASTIA_CHECK(mount.mkdir("/a", directory_mode()) == 0);
    ASTIA_CHECK(mount.mkdir("/a/child", directory_mode()) == 0);

    const astia::u64 inode = mount.node_inode("/a");

    ASTIA_CHECK(inode != 0);
    ASTIA_CHECK(mount.rename("/a", "/b") == 0);

    char path[astia::fs::path_max]{};

    ASTIA_CHECK(mount.node_path(inode, path, sizeof path) == 0);
    ASTIA_CHECK(std::strcmp(path, "/b") == 0);

    ASTIA_CHECK(mount.node_path(0, path, sizeof path) == 0);
    ASTIA_CHECK(std::strcmp(path, "/") == 0);

    ASTIA_CHECK(mount.node_path(~astia::u64{0}, path, sizeof path) == -ENOENT);

    char small[2];

    ASTIA_CHECK(mount.node_path(inode, small, sizeof small) == -ENAMETOOLONG);
}
