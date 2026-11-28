#include "arena.hpp"
#include "heap_exhaustion.hpp"
#include "test.hpp"

#ifdef RENAME_NOREPLACE
#undef RENAME_NOREPLACE
#endif
#ifdef RENAME_EXCHANGE
#undef RENAME_EXCHANGE
#endif

#include <astia/api/errno.h>
#include <astia/fs.hpp>
#include <astia/mm/frame_allocator.hpp>
#include <astia/mm/heap.hpp>

#include "../src/fs/path_service.hpp"

#include <cstring>

namespace {
    bool same(const char* lhs, const char* rhs) {
        return std::strcmp(lhs, rhs) == 0;
    }

    bool is_directory(const char* path) {
        return same(path, "/") || same(path, "/tree") || same(path, "/tree/") ||
               same(path, "/tree/sub") || same(path, "/jail") ||
               same(path, "/jail/etc") || same(path, "/jail/tree") ||
               same(path, "/jail/tree/sub");
    }

    bool is_file(const char* path) {
        return same(path, "/tree/file") || same(path, "/target") ||
               same(path, "/tree/target") || same(path, "/jail/etc/passwd") ||
               same(path, "/jail/tree/file") || same(path, "/jail/target");
    }

    const char* symlink_target(const char* path) {
        if (same(path, "/tree/link"))
            return "file";

        if (same(path, "/absolute"))
            return "/target";

        if (same(path, "/dangling"))
            return "/does/not/exist";

        if (same(path, "/loop1"))
            return "/loop2";

        if (same(path, "/loop2"))
            return "/loop1";

        if (same(path, "/jail/passwd-link"))
            return "/etc/passwd";

        if (same(path, "/jail/up"))
            return "../../etc/passwd";

        if (same(path, "/jail/rel"))
            return "etc/passwd";

        return nullptr;
    }

    alignas(16) astia::u8 path_service_heap_storage[64 * 1024];

    bool no_mapping(astia::u64, astia::u64) {
        return true;
    }

    void initialize_heap() {
        static bool ready = false;
        if (ready)
            return;

        astia_test::map_arena();

        astia::mm::memory_map map;

        map.add(astia_test::arena_base, astia_test::arena_size,
                astia::mm::memory_type::usable);
        astia::mm::kernel_frames().init(map);
        astia::mm::heap::configure(path_service_heap_storage,
                                   sizeof path_service_heap_storage, no_mapping);
        ASTIA_CHECK(astia::mm::heap::init());

        ready = true;
    }
}

namespace astia::fs {
    file_type node_type(const char* path) {
        if (path == nullptr)
            return directory_unknown;

        if (is_directory(path))
            return directory_dir;

        if (is_file(path))
            return directory_reg;

        if (symlink_target(path) != nullptr)
            return directory_lnk;

        return directory_unknown;
    }

    long read_symlink(const char* path, char* buffer, u32 capacity) {
        const char* target = symlink_target(path);
        if (target == nullptr)
            return -ENOENT;

        const usz length = std::strlen(target);

        if (capacity <= length)
            return -ENAMETOOLONG;

        std::memcpy(buffer, target, length + 1);

        return static_cast<long>(length);
    }
}

ASTIA_TEST(path_service_composes_absolute_and_relative_paths) {
    char buffer[astia::fs::path_max]{};

    ASTIA_CHECK(astia::fs::path_service::compose(
                    "/work", "/absolute", astia::span<char>(buffer, sizeof buffer)) == 0);
    ASTIA_CHECK(std::strcmp(buffer, "/absolute") == 0);
    ASTIA_CHECK(astia::fs::path_service::compose(
                    "/work", ".", astia::span<char>(buffer, sizeof buffer)) == 0);
    ASTIA_CHECK(std::strcmp(buffer, "/work") == 0);
    ASTIA_CHECK(astia::fs::path_service::compose(
                    "/work", "child", astia::span<char>(buffer, sizeof buffer)) == 0);
    ASTIA_CHECK(std::strcmp(buffer, "/work/child") == 0);

    char short_buffer[5]{};

    ASTIA_CHECK(astia::fs::path_service::compose(
                    "/work", ".", astia::span<char>(short_buffer, sizeof short_buffer)) ==
                -ERANGE);
    ASTIA_CHECK(
        astia::fs::path_service::compose(
            "/work", "child", astia::span<char>(short_buffer, sizeof short_buffer)) ==
        -ENAMETOOLONG);
}

ASTIA_TEST(path_service_normalizes_dotdot_and_symlinks) {
    initialize_heap();

    char buffer[astia::fs::path_max]{};

    ASTIA_CHECK(astia::fs::path_service::resolve("/tree/sub/..", buffer, sizeof buffer,
                                                 false) == 0);
    ASTIA_CHECK(std::strcmp(buffer, "/tree") == 0);

    ASTIA_CHECK(astia::fs::path_service::resolve("/tree/sub/../file", buffer,
                                                 sizeof buffer, false) == 0);
    ASTIA_CHECK(std::strcmp(buffer, "/tree/file") == 0);

    ASTIA_CHECK(astia::fs::path_service::resolve("/tree/sub/../..", buffer, sizeof buffer,
                                                 false) == 0);
    ASTIA_CHECK(std::strcmp(buffer, "/") == 0);

    ASTIA_CHECK(astia::fs::path_service::resolve("/tree/sub/../target", buffer,
                                                 sizeof buffer, true) == 0);
    ASTIA_CHECK(std::strcmp(buffer, "/tree/target") == 0);

    ASTIA_CHECK(astia::fs::path_service::resolve("/tree/../target", buffer, sizeof buffer,
                                                 false) == 0);
    ASTIA_CHECK(std::strcmp(buffer, "/target") == 0);
    ASTIA_CHECK(astia::fs::path_service::resolve("/tree/link", buffer, sizeof buffer) ==
                0);
    ASTIA_CHECK(std::strcmp(buffer, "/tree/file") == 0);
    ASTIA_CHECK(astia::fs::path_service::resolve("/tree/link", buffer, sizeof buffer,
                                                 false) == 0);
    ASTIA_CHECK(std::strcmp(buffer, "/tree/link") == 0);
    ASTIA_CHECK(astia::fs::path_service::resolve("/absolute", buffer, sizeof buffer) ==
                0);
    ASTIA_CHECK(std::strcmp(buffer, "/target") == 0);
    ASTIA_CHECK(astia::fs::path_service::resolve("/tree/file/child", buffer,
                                                 sizeof buffer) == -ENOTDIR);
    ASTIA_CHECK(
        astia::fs::path_service::resolve("/dangling", buffer, sizeof buffer, false) == 0);
    ASTIA_CHECK(std::strcmp(buffer, "/dangling") == 0);
    ASTIA_CHECK(astia::fs::path_service::resolve("/dangling", buffer, sizeof buffer,
                                                 true) == -ENOENT);
}

ASTIA_TEST(path_service_preserves_loop_and_length_errors) {
    initialize_heap();

    char buffer[astia::fs::path_max]{};

    ASTIA_CHECK(astia::fs::path_service::resolve("/loop1", buffer, sizeof buffer) ==
                -ELOOP);
    ASTIA_CHECK(astia::fs::path_service::resolve("/tree/file/", buffer, sizeof buffer) ==
                -ENOTDIR);

    char long_path[astia::fs::path_max + 1]{};

    long_path[0] = '/';

    for (astia::u32 i = 1; i < sizeof long_path - 1; ++i) {
        long_path[i] = 'x';
    }

    ASTIA_CHECK(astia::fs::path_service::resolve(long_path, buffer, sizeof buffer) ==
                -ENAMETOOLONG);
}

ASTIA_TEST(path_service_resolves_at_path_max) {
    initialize_heap();

    char path[astia::fs::path_max]{};
    char buffer[astia::fs::path_max]{};

    path[0] = '/';

    for (astia::u32 i = 1; i < sizeof path - 1; ++i) {
        path[i] = 'x';
    }

    ASTIA_CHECK(astia::fs::path_service::resolve(path, buffer, sizeof buffer) == 0);
    ASTIA_CHECK(std::strlen(buffer) == sizeof path - 1);
}

ASTIA_TEST(path_service_resolves_absolute_paths_from_a_process_root) {
    initialize_heap();

    char buffer[astia::fs::path_max]{};

    ASTIA_CHECK(astia::fs::path_service::resolve_from("/jail", "/etc/passwd", buffer,
                                                      sizeof buffer, true) == 0);
    ASTIA_CHECK(same(buffer, "/jail/etc/passwd"));

    ASTIA_CHECK(astia::fs::path_service::resolve_from("/jail", "/tree/../etc/passwd",
                                                      buffer, sizeof buffer, true) == 0);
    ASTIA_CHECK(same(buffer, "/jail/etc/passwd"));

    ASTIA_CHECK(astia::fs::path_service::resolve_from("/jail", "/..", buffer,
                                                      sizeof buffer, true) == 0);
    ASTIA_CHECK(same(buffer, "/jail"));

    ASTIA_CHECK(astia::fs::path_service::resolve_from("/jail", "/../../..", buffer,
                                                      sizeof buffer, true) == 0);
    ASTIA_CHECK(same(buffer, "/jail"));

    ASTIA_CHECK(astia::fs::path_service::resolve_from("/jail", "/tree/file/child", buffer,
                                                      sizeof buffer, true) == -ENOTDIR);
}

ASTIA_TEST(path_service_rebases_symlinks_inside_the_process_root) {
    initialize_heap();

    char buffer[astia::fs::path_max]{};

    ASTIA_CHECK(astia::fs::path_service::resolve_from("/jail", "/passwd-link", buffer,
                                                      sizeof buffer, true) == 0);
    ASTIA_CHECK(same(buffer, "/jail/etc/passwd"));

    ASTIA_CHECK(astia::fs::path_service::resolve_from("/jail", "/up", buffer,
                                                      sizeof buffer, true) == 0);
    ASTIA_CHECK(same(buffer, "/jail/etc/passwd"));

    ASTIA_CHECK(astia::fs::path_service::resolve_from("/jail", "/rel", buffer,
                                                      sizeof buffer, true) == 0);
    ASTIA_CHECK(same(buffer, "/jail/etc/passwd"));

    ASTIA_CHECK(astia::fs::path_service::resolve_from("/jail", "/passwd-link", buffer,
                                                      sizeof buffer, false) == 0);
    ASTIA_CHECK(same(buffer, "/jail/passwd-link"));
}

ASTIA_TEST(path_service_checks_root_membership_at_component_boundaries) {
    ASTIA_CHECK(astia::fs::path_service::within_root("/", "/anything"));
    ASTIA_CHECK(astia::fs::path_service::within_root("/jail", "/jail"));
    ASTIA_CHECK(astia::fs::path_service::within_root("/jail", "/jail/etc"));
    ASTIA_CHECK(!astia::fs::path_service::within_root("/jail", "/jailed"));
    ASTIA_CHECK(!astia::fs::path_service::within_root("/jail", "/"));
    ASTIA_CHECK(!astia::fs::path_service::within_root("jail", "/jail"));
}

ASTIA_TEST(path_service_resolve_reports_enomem_when_scratch_allocation_fails) {
    initialize_heap();

    char buffer[astia::fs::path_max]{};

    {
        astia_test::heap_exhaustion exhaustion;

        ASTIA_CHECK(exhaustion.acquire());
        ASTIA_CHECK(astia::fs::path_service::resolve("/target", buffer, sizeof buffer) ==
                    -ENOMEM);
    }

    ASTIA_CHECK(astia::fs::path_service::resolve("/target", buffer, sizeof buffer) == 0);
    ASTIA_CHECK(same(buffer, "/target"));
}
