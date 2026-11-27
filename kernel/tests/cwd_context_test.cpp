#include <astia/api/errno.h>
#include <astia/arch/cpu.hpp>
#include <astia/mm/frame_allocator.hpp>
#include <astia/mm/heap.hpp>

#include "../src/proc/cwd_context.hpp"
#include "arena.hpp"
#include "support/test.hpp"

#include <cstring>

namespace {
    alignas(16) astia::u8 cwd_context_heap_storage[64 * 1024];

    bool resolution_irq_open   = false;
    bool resolution_irq_masked = false;

    void note_resolution_irq_state() {
        if (astia::arch::cpu::check_interrupts())
            resolution_irq_open = true;
        else
            resolution_irq_masked = true;
    }

    bool no_mapping(astia::u64, astia::u64) {
        return true;
    }

    astia::i64 run(astia::core::task<astia::i64> operation) {
        if (!operation.valid())
            return -ENOMEM;

        operation.resume();

        if (!operation.done())
            return -EAGAIN;

        if (operation.failed())
            return -astia::error_errno(operation.error());

        return *operation.value();
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
        astia::mm::heap::configure(cwd_context_heap_storage,
                                   sizeof cwd_context_heap_storage, no_mapping);
        ASTIA_CHECK(astia::mm::heap::init());

        ready = true;
    }
} // namespace

namespace astia::fs {
    namespace {
        bool drop_component(char* path) {
            usize length = std::strlen(path);

            while (length > 1 && path[length - 1] == '/') {
                --length;
            }

            while (length > 1 && path[length - 1] != '/') {
                --length;
            }

            if (length > 1)
                --length;

            path[length == 0 ? 1 : length] = '\0';

            return length != 0;
        }

        long join_root(const char* root, const char* path, char* buffer, u32 capacity) {
            char  joined[64]{};
            usize at = 0;

            if (std::strcmp(root, "/") == 0) {
                for (; path[at] != '\0' && at + 1 < sizeof joined; ++at) {
                    joined[at] = path[at];
                }
            }
            else if (std::strcmp(path, "/") == 0) {
                for (; root[at] != '\0' && at + 1 < sizeof joined; ++at) {
                    joined[at] = root[at];
                }
            }
            else {
                for (; root[at] != '\0' && at + 1 < sizeof joined; ++at) {
                    joined[at] = root[at];
                }

                for (usize i = 0; path[i] != '\0' && at + 1 < sizeof joined; ++i) {
                    joined[at++] = path[i];
                }
            }

            joined[at] = '\0';

            while (true) {
                usize length = std::strlen(joined);
                char  candidate[64];

                std::strcpy(candidate, joined);

                if (std::strcmp(candidate, "/good/..") == 0) {
                    std::strcpy(joined, "/");
                    continue;
                }

                bool dotted = false;

                if (length >= 2 && std::strcmp(candidate + length - 2, "/.") == 0) {
                    candidate[length - 2] = '\0';

                    if (std::strlen(candidate) == 0)
                        std::strcpy(candidate, "/");

                    std::strcpy(joined, candidate);

                    dotted = true;
                }
                else if (length >= 3 && std::strcmp(candidate + length - 3, "/..") == 0) {
                    candidate[length - 3] = '\0';

                    if (std::strlen(candidate) == 0)
                        std::strcpy(candidate, "/");

                    std::strcpy(joined, candidate);

                    if (std::strcmp(joined, root) != 0 &&
                        std::strlen(joined) > std::strlen(root))
                        (void) drop_component(joined);

                    dotted = true;
                }

                if (!dotted)
                    break;
            }

            if (std::strlen(joined) + 1 > capacity)
                return -ENAMETOOLONG;

            std::strcpy(buffer, joined);

            return 0;
        }
    } // namespace

    long resolve_path_from(const char* root, const char* path, char* buffer, u32 capacity,
                           bool) {
        note_resolution_irq_state();

        if (root == nullptr || path == nullptr || buffer == nullptr || capacity == 0)
            return -EINVAL;

        if (root[0] != '/' || path[0] != '/')
            return -EINVAL;

        return join_root(root, path, buffer, capacity);
    }

    file_type node_type(const char* path) {
        note_resolution_irq_state();

        if (std::strcmp(path, "/") == 0 || std::strcmp(path, "/good") == 0 ||
            std::strcmp(path, "/jail") == 0 || std::strcmp(path, "/jail/etc") == 0)
            return directory_dir;

        if (std::strcmp(path, "/file") == 0 || std::strcmp(path, "/jail/etc/passwd") == 0)
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
    initialize_heap();

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
    initialize_heap();

    astia::proc::cwd_context context;

    char output[32];

    ASTIA_CHECK(run(context.change_directory("/good")) == 0);
    ASTIA_CHECK(context.copy_cwd(output, sizeof output) == 6);
    ASTIA_CHECK(std::strcmp(output, "/good") == 0);
    ASTIA_CHECK(run(context.change_directory("/file")) == -ENOTDIR);
    ASTIA_CHECK(run(context.change_directory("/missing")) == -ENOENT);
    ASTIA_CHECK(context.copy_cwd(output, sizeof output) == 6);
    ASTIA_CHECK(std::strcmp(output, "/good") == 0);
    ASTIA_CHECK(run(context.change_directory("..")) == 0);
    ASTIA_CHECK(std::strcmp(output, "/good") == 0);
    ASTIA_CHECK(context.copy_cwd(output, sizeof output) == 2);
    ASTIA_CHECK(std::strcmp(output, "/") == 0);
}

ASTIA_TEST(cwd_context_resolves_paths_with_interrupts_enabled) {
    initialize_heap();

    astia::proc::cwd_context context;

    resolution_irq_open   = false;
    resolution_irq_masked = false;

    ASTIA_CHECK(run(context.change_directory("/good")) == 0);
    ASTIA_CHECK(resolution_irq_open);
    ASTIA_CHECK(!resolution_irq_masked);
}

ASTIA_TEST(cwd_context_keeps_the_anchor_after_a_failed_directory_change) {
    initialize_heap();

    astia::proc::cwd_context context;

    astia::fs::directory_matches_stale = false;

    char output[32];

    ASTIA_CHECK(context.bind_directory("/anchored", make_anchor()) == 0);
    ASTIA_CHECK(run(context.change_directory("/missing")) == -ENOENT);
    ASTIA_CHECK(run(context.change_directory("/file")) == -ENOTDIR);

    astia::fs::directory_matches_stale = true;

    ASTIA_CHECK(context.compose_path("child", output, sizeof output) == -ENOENT);
    ASTIA_CHECK(context.copy_cwd(output, sizeof output) ==
                static_cast<astia::i64>(std::strlen("/anchored") + 1));
    ASTIA_CHECK(std::strcmp(output, "/anchored") == 0);

    astia::fs::directory_matches_stale = false;
}

ASTIA_TEST(cwd_context_inherits_a_snapshot) {
    initialize_heap();

    astia::proc::cwd_context parent;
    astia::proc::cwd_context child;

    char output[32];

    ASTIA_CHECK(run(parent.change_directory("/good")) == 0);
    child.inherit_from(parent);
    ASTIA_CHECK(child.copy_cwd(output, sizeof output) == 6);
    ASTIA_CHECK(std::strcmp(output, "/good") == 0);
}

ASTIA_TEST(cwd_context_binds_an_open_directory_without_resolving) {
    initialize_heap();

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
    initialize_heap();

    astia::proc::cwd_context context;

    astia::fs::directory_matches_stale = false;

    char output[32];

    ASTIA_CHECK(context.bind_directory("/recycled", make_anchor()) == 0);

    astia::fs::directory_matches_stale = true;

    ASTIA_CHECK(context.compose_path("child", output, sizeof output) == -ENOENT);
    ASTIA_CHECK(run(context.change_directory("child")) == -ENOENT);
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
    initialize_heap();

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

    ASTIA_CHECK(run(parent.change_directory("/good")) == 0);

    astia::fs::directory_matches_stale = true;

    ASTIA_CHECK(parent.compose_path("child", output, sizeof output) == 0);
    ASTIA_CHECK(std::strcmp(output, "/good/child") == 0);

    astia::fs::directory_matches_stale = false;
}

ASTIA_TEST(cwd_context_clamps_paths_to_the_process_root) {
    initialize_heap();

    astia::proc::cwd_context context;

    char output[32];
    char resolved[32];

    ASTIA_CHECK(context.change_root("/jail") == 0);
    ASTIA_CHECK(context.copy_root(output, sizeof output) == 6);
    ASTIA_CHECK(std::strcmp(output, "/jail") == 0);

    ASTIA_CHECK(context.copy_cwd(output, sizeof output) == -ENOENT);
    ASTIA_CHECK(run(context.change_directory("etc")) == -ENOENT);

    ASTIA_CHECK(run(context.change_directory("/")) == 0);
    ASTIA_CHECK(context.copy_cwd(output, sizeof output) == 2);
    ASTIA_CHECK(std::strcmp(output, "/") == 0);

    ASTIA_CHECK(run(context.change_directory("etc")) == 0);
    ASTIA_CHECK(context.copy_cwd(output, sizeof output) == 5);
    ASTIA_CHECK(std::strcmp(output, "/etc") == 0);

    ASTIA_CHECK(run(context.change_directory("..")) == 0);
    ASTIA_CHECK(context.copy_cwd(output, sizeof output) == 2);
    ASTIA_CHECK(run(context.change_directory("../..")) == 0);
    ASTIA_CHECK(context.copy_cwd(output, sizeof output) == 2);

    ASTIA_CHECK(context.resolve_path("/etc/passwd", resolved, sizeof resolved, true) ==
                0);
    ASTIA_CHECK(std::strcmp(resolved, "/jail/etc/passwd") == 0);

    ASTIA_CHECK(context.change_root("/") == -EACCES);
    ASTIA_CHECK(context.change_root("/jail/etc") == 0);
    ASTIA_CHECK(context.copy_root(output, sizeof output) == 10);
    ASTIA_CHECK(std::strcmp(output, "/jail/etc") == 0);
}

ASTIA_TEST(cwd_context_inherits_root_state) {
    initialize_heap();

    astia::proc::cwd_context parent;
    astia::proc::cwd_context child;

    char output[32];

    ASTIA_CHECK(run(parent.change_directory("/")) == 0);
    ASTIA_CHECK(parent.change_root("/jail") == 0);
    child.inherit_from(parent);

    ASTIA_CHECK(child.copy_root(output, sizeof output) == 6);
    ASTIA_CHECK(std::strcmp(output, "/jail") == 0);
    ASTIA_CHECK(child.copy_cwd(output, sizeof output) == -ENOENT);
}
