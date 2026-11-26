#include <astia/api/errno.h>

#include "../src/proc/cwd_context.hpp"
#include "support/test.hpp"

#include <cstring>

namespace astia::fs {
    long resolve_path(const char* path, char* buffer, u32 capacity, bool) {
        if (std::strcmp(path, "/good/..") == 0)
            path = "/";

        const usize length = std::strlen(path);

        if (length + 1 > capacity)
            return -ENAMETOOLONG;

        std::memcpy(buffer, path, length + 1);

        return 0;
    }

    file_type node_type(const char* path) {
        if (std::strcmp(path, "/") == 0 || std::strcmp(path, "/good") == 0)
            return directory_dir;

        if (std::strcmp(path, "/file") == 0)
            return directory_reg;

        return directory_unknown;
    }

    bool directory_matches_stale = false;

    bool directory_matches(const char*, mount*, u64) {
        return !directory_matches_stale;
    }
} // namespace astia::fs

namespace {
    alignas(16) astia::u8 anchor_storage[64];

    astia::proc::directory_anchor make_anchor() {
        return astia::proc::directory_anchor{
            astia::mm::shptr<astia::fs::mount>::borrowed(
                reinterpret_cast<astia::fs::mount*>(anchor_storage)),
            7};
    }
} // namespace

ASTIA_TEST(cwd_context_composes_without_partial_output) {
    astia::proc::cwd_context context;

    char output[32];

    ASTIA_CHECK(context.compose_path("bin", output, sizeof output) == 0);
    ASTIA_CHECK(std::strcmp(output, "/bin") == 0);
    ASTIA_CHECK(context.compose_path(".", output, sizeof output) == 0);
    ASTIA_CHECK(std::strcmp(output, "/") == 0);

    char small[4] = {'x', 'x', 'x', 'x'};

    ASTIA_CHECK(context.compose_path("long", small, sizeof small) == -ENAMETOOLONG);
    ASTIA_CHECK(small[0] == 'x' && small[1] == 'x' && small[2] == 'x' && small[3] == 'x');
    ASTIA_CHECK(context.compose_path_at("/tmp", "file", output, sizeof output) == 0);
    ASTIA_CHECK(std::strcmp(output, "/tmp/file") == 0);
}

ASTIA_TEST(cwd_context_commits_only_successful_directory_changes) {
    astia::proc::cwd_context context;

    char output[32];

    ASTIA_CHECK(context.change_directory("/good") == 0);
    ASTIA_CHECK(context.copy_cwd(output, sizeof output) == 6);
    ASTIA_CHECK(std::strcmp(output, "/good") == 0);
    ASTIA_CHECK(context.change_directory("/file") == -ENOTDIR);
    ASTIA_CHECK(context.change_directory("/missing") == -ENOENT);
    ASTIA_CHECK(context.copy_cwd(output, sizeof output) == 6);
    ASTIA_CHECK(std::strcmp(output, "/good") == 0);
    ASTIA_CHECK(context.change_directory("..") == 0);
    ASTIA_CHECK(std::strcmp(output, "/good") == 0);
    ASTIA_CHECK(context.copy_cwd(output, sizeof output) == 2);
    ASTIA_CHECK(std::strcmp(output, "/") == 0);
}

ASTIA_TEST(cwd_context_inherits_a_snapshot) {
    astia::proc::cwd_context parent;
    astia::proc::cwd_context child;

    char output[32];

    ASTIA_CHECK(parent.change_directory("/good") == 0);
    child.inherit_from(parent);
    ASTIA_CHECK(child.copy_cwd(output, sizeof output) == 6);
    ASTIA_CHECK(std::strcmp(output, "/good") == 0);
}

ASTIA_TEST(cwd_context_binds_an_open_directory_without_resolving) {
    astia::proc::cwd_context context;

    astia::fs::directory_matches_stale = false;

    char output[32];
    char long_path[astia::fs::path_max + 1];

    ASTIA_CHECK(context.bind_directory("/vanished", make_anchor()) == 0);
    ASTIA_CHECK(context.copy_cwd(output, sizeof output) ==
                static_cast<astia::i64>(std::strlen("/vanished") + 1));
    ASTIA_CHECK(std::strcmp(output, "/vanished") == 0);
    ASTIA_CHECK(context.compose_path("child", output, sizeof output) == 0);
    ASTIA_CHECK(std::strcmp(output, "/vanished/child") == 0);

    ASTIA_CHECK(context.bind_directory("relative", make_anchor()) == -EINVAL);

    for (astia::usize i = 0; i < sizeof(long_path) - 1; ++i) {
        long_path[i] = 'x';
    }

    long_path[0]                     = '/';
    long_path[sizeof(long_path) - 1] = '\0';

    ASTIA_CHECK(context.bind_directory(long_path, make_anchor()) == -ENAMETOOLONG);
    ASTIA_CHECK(context.copy_cwd(output, sizeof output) ==
                static_cast<astia::i64>(std::strlen("/vanished") + 1));
    ASTIA_CHECK(std::strcmp(output, "/vanished") == 0);
}

ASTIA_TEST(cwd_context_rejects_a_recycled_directory_name) {
    astia::proc::cwd_context context;

    astia::fs::directory_matches_stale = false;

    char output[32];

    ASTIA_CHECK(context.bind_directory("/recycled", make_anchor()) == 0);

    astia::fs::directory_matches_stale = true;

    ASTIA_CHECK(context.compose_path("child", output, sizeof output) == -ENOENT);
    ASTIA_CHECK(context.change_directory("child") == -ENOENT);
    ASTIA_CHECK(context.compose_path("/absolute", output, sizeof output) == 0);
    ASTIA_CHECK(std::strcmp(output, "/absolute") == 0);
    ASTIA_CHECK(context.copy_cwd(output, sizeof output) ==
                static_cast<astia::i64>(std::strlen("/recycled") + 1));
    ASTIA_CHECK(std::strcmp(output, "/recycled") == 0);

    astia::fs::directory_matches_stale = false;

    ASTIA_CHECK(context.compose_path("child", output, sizeof output) == 0);
    ASTIA_CHECK(std::strcmp(output, "/recycled/child") == 0);
}

ASTIA_TEST(cwd_context_inherits_a_bound_directory) {
    astia::proc::cwd_context parent;
    astia::proc::cwd_context child;

    astia::fs::directory_matches_stale = false;

    char output[32];

    ASTIA_CHECK(parent.bind_directory("/bound", make_anchor()) == 0);
    child.inherit_from(parent);

    astia::fs::directory_matches_stale = true;

    ASTIA_CHECK(child.compose_path("child", output, sizeof output) == -ENOENT);
    ASTIA_CHECK(parent.compose_path("child", output, sizeof output) == -ENOENT);

    astia::fs::directory_matches_stale = false;

    ASTIA_CHECK(parent.change_directory("/good") == 0);

    astia::fs::directory_matches_stale = true;

    ASTIA_CHECK(parent.compose_path("child", output, sizeof output) == 0);
    ASTIA_CHECK(std::strcmp(output, "/good/child") == 0);

    astia::fs::directory_matches_stale = false;
}
