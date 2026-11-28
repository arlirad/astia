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

    void initialize_filesystem() {
        initialize_runtime();

        static bool initialized = false;
        if (initialized)
            return;

        ASTIA_CHECK(astia::fs::init());

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

ASTIA_TEST(tmpfs_node_limit_reports_enospc_for_a_new_file) {
    initialize_runtime();

    astia::fs::filesystems::tmpfs_mount mount{2};

    auto first = mount.open("/first", ASTIA_AIO_O_CREAT | ASTIA_AIO_O_RDWR);

    ASTIA_CHECK(static_cast<bool>(first));

    auto second = mount.open("/second", ASTIA_AIO_O_CREAT | ASTIA_AIO_O_RDWR);

    ASTIA_CHECK(static_cast<bool>(second));

    long error = 0;

    auto third = mount.open("/third", ASTIA_AIO_O_CREAT | ASTIA_AIO_O_RDWR, &error);

    ASTIA_CHECK(!static_cast<bool>(third));
    ASTIA_CHECK(error == -ENOSPC);

    long missing_error = 0;

    auto missing = mount.open("/missing", 0, &missing_error);

    ASTIA_CHECK(!static_cast<bool>(missing));
    ASTIA_CHECK(missing_error == -ENOENT);

    ASTIA_CHECK(mount.mkdir("/fourth", directory_mode()) == -ENOSPC);
}

ASTIA_TEST(tmpfs_mount_source_selects_the_node_budget) {
    initialize_runtime();

    astia::fs::filesystems::tmpfs type;

    astia::fs::mount* default_mount = type.mount(nullptr);

    ASTIA_CHECK(default_mount != nullptr);
    type.destroy_mount(default_mount);

    astia::fs::mount* limited = type.mount("nodes=2");

    ASTIA_CHECK(limited != nullptr);

    if (limited == nullptr)
        return;

    auto* budget = static_cast<astia::fs::filesystems::tmpfs_mount*>(limited);

    long error = 0;

    ASTIA_CHECK(static_cast<bool>(budget->open("/one", ASTIA_AIO_O_CREAT, &error)));
    ASTIA_CHECK(error == 0);
    ASTIA_CHECK(static_cast<bool>(budget->open("/two", ASTIA_AIO_O_CREAT, &error)));

    ASTIA_CHECK(!static_cast<bool>(budget->open("/three", ASTIA_AIO_O_CREAT, &error)));
    ASTIA_CHECK(error == -ENOSPC);

    type.destroy_mount(limited);

    ASTIA_CHECK(type.mount("nodes=0") == nullptr);
    ASTIA_CHECK(type.mount("capacity=2") == nullptr);
}

ASTIA_TEST(tmpfs_open_rejects_directory_create_combination) {
    initialize_runtime();

    astia::fs::filesystems::tmpfs_mount mount;

    ASTIA_CHECK(mount.mkdir("/d", directory_mode()) == 0);

    long error = 0;

    ASTIA_CHECK(
        !mount.open("/d/missing", ASTIA_AIO_O_CREAT | ASTIA_AIO_O_DIRECTORY, &error));
    ASTIA_CHECK(error == -EINVAL);

    error = 0;

    ASTIA_CHECK(!mount.open("/d", ASTIA_AIO_O_CREAT | ASTIA_AIO_O_DIRECTORY, &error));
    ASTIA_CHECK(error == -EINVAL);

    ASTIA_CHECK(mount.open("/d", ASTIA_AIO_O_DIRECTORY));
    ASTIA_CHECK(mount.open("/d/created", ASTIA_AIO_O_CREAT));
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

ASTIA_TEST(fs_directory_path_follows_a_renamed_open_directory) {
    initialize_filesystem();

    (void) astia::fs::remove_path("/tmp/dirfd-before/marker", false);
    (void) astia::fs::remove_path("/tmp/dirfd-after/child", true);
    (void) astia::fs::remove_path("/tmp/dirfd-after", true);
    (void) astia::fs::remove_path("/tmp/dirfd-before", true);

    ASTIA_CHECK(astia::fs::make_directory("/tmp/dirfd-before", directory_mode()) == 0);
    ASTIA_CHECK(astia::fs::make_directory("/tmp/dirfd-before/child", directory_mode()) ==
                0);

    auto directory = astia::fs::open_directory("/tmp/dirfd-before");

    ASTIA_CHECK(directory);

    char path[astia::fs::path_max]{};

    ASTIA_CHECK(astia::fs::directory_path(*directory, path, sizeof path) == 0);
    ASTIA_CHECK(std::strcmp(path, "/tmp/dirfd-before") == 0);

    ASTIA_CHECK(astia::fs::rename_path("/tmp/dirfd-before", "/tmp/dirfd-after") == 0);

    ASTIA_CHECK(astia::fs::directory_path(*directory, path, sizeof path) == 0);
    ASTIA_CHECK(std::strcmp(path, "/tmp/dirfd-after") == 0);

    ASTIA_CHECK(astia::fs::make_directory("/tmp/dirfd-before", directory_mode()) == 0);
    ASTIA_CHECK(static_cast<bool>(astia::fs::open_creating(
        "/tmp/dirfd-before/marker", ASTIA_AIO_O_CREAT | ASTIA_AIO_O_WRONLY, {})));

    ASTIA_CHECK(astia::fs::directory_path(*directory, path, sizeof path) == 0);
    ASTIA_CHECK(std::strcmp(path, "/tmp/dirfd-after") == 0);

    char name[256]{};

    astia::fs::file_type type = astia::fs::directory_unknown;

    ASTIA_CHECK(
        astia::fs::read_directory(directory, 0, name, sizeof name, &type, nullptr) == 1);
    ASTIA_CHECK(std::strcmp(name, "child") == 0);

    char small[3];

    ASTIA_CHECK(astia::fs::directory_path(*directory, small, sizeof small) ==
                -ENAMETOOLONG);

    ASTIA_CHECK(astia::fs::remove_path("/tmp/dirfd-before/marker", false) == 0);
    ASTIA_CHECK(astia::fs::remove_path("/tmp/dirfd-before", true) == 0);
    ASTIA_CHECK(astia::fs::remove_path("/tmp/dirfd-after/child", true) == 0);
    ASTIA_CHECK(astia::fs::remove_path("/tmp/dirfd-after", true) == 0);
}

ASTIA_TEST(fs_directory_path_rejects_an_unlinked_open_directory) {
    initialize_filesystem();

    (void) astia::fs::remove_path("/tmp/dirfd-gone", true);

    ASTIA_CHECK(astia::fs::make_directory("/tmp/dirfd-gone", directory_mode()) == 0);

    auto directory = astia::fs::open_directory("/tmp/dirfd-gone");

    ASTIA_CHECK(directory);

    ASTIA_CHECK(astia::fs::remove_path("/tmp/dirfd-gone", true) == 0);

    char path[astia::fs::path_max]{};

    ASTIA_CHECK(astia::fs::directory_path(*directory, path, sizeof path) == -ENOENT);
}
