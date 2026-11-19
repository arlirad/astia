#include "test.hpp"

#ifdef RENAME_NOREPLACE
#undef RENAME_NOREPLACE
#endif
#ifdef RENAME_EXCHANGE
#undef RENAME_EXCHANGE
#endif

#include <astia/api/errno.h>
#include <astia/fs.hpp>
#include "../src/fs/path_service.hpp"

#include <cstring>

namespace {
    bool same(const char* lhs, const char* rhs) {
        return std::strcmp(lhs, rhs) == 0;
    }

    bool is_directory(const char* path) {
        return same(path, "/") || same(path, "/tree") || same(path, "/tree/");
    }

    bool is_file(const char* path) {
        return same(path, "/tree/file") || same(path, "/target") ||
               same(path, "/tree/target");
    }

    const char* symlink_target(const char* path) {
        if (same(path, "/tree/link"))
            return "file";
        if (same(path, "/absolute"))
            return "/target";
        if (same(path, "/loop1"))
            return "/loop2";
        if (same(path, "/loop2"))
            return "/loop1";
        return nullptr;
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
        const usize length = std::strlen(target);
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
    char buffer[astia::fs::path_max]{};
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
}

ASTIA_TEST(path_service_preserves_loop_and_length_errors) {
    char buffer[astia::fs::path_max]{};
    ASTIA_CHECK(astia::fs::path_service::resolve("/loop1", buffer, sizeof buffer) ==
                -ELOOP);
    ASTIA_CHECK(astia::fs::path_service::resolve("/tree/file/", buffer, sizeof buffer) ==
                -ENOTDIR);

    char long_path[astia::fs::path_max + 1]{};
    long_path[0] = '/';
    for (astia::u32 i = 1; i < sizeof long_path - 1; ++i)
        long_path[i] = 'x';
    ASTIA_CHECK(astia::fs::path_service::resolve(long_path, buffer, sizeof buffer) ==
                -ENAMETOOLONG);
}
