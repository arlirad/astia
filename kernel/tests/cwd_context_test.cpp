#include "../src/proc/cwd_context.hpp"
#include "support/test.hpp"
#include <astia/api/errno.h>
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

} // namespace astia::fs

ASTIA_TEST(cwd_context_composes_without_partial_output) {
    astia::proc::cwd_context context;
    char                    output[32];
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
    char                    output[32];
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
    char                    output[32];
    ASTIA_CHECK(parent.change_directory("/good") == 0);
    child.inherit_from(parent);
    ASTIA_CHECK(child.copy_cwd(output, sizeof output) == 6);
    ASTIA_CHECK(std::strcmp(output, "/good") == 0);
}
