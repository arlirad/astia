#include <astia/init.hpp>

#ifndef ASTIA_HOST_TEST
#include "log.hpp"
#endif

namespace astia::init {

    namespace {

#ifndef ASTIA_HOST_TEST
        auto log_line(const char* prefix, const char* name) -> void {
            astia::log::info("%s%s", prefix, name);
        }
#else
        // Host tests exercise the ordering rules only; diagnostics are silent.
        auto log_line(const char*, const char*) -> void {}
#endif

        // The step table is bounded by a fixed size so the ordering pass needs
        // no dynamic allocation (it runs before, and independent of, the frame
        // allocator).
        constexpr usize max_steps = 128;

        enum class state : u8 { pending, ok, failed, skipped };

        auto names_equal(const char* a, const char* b) -> bool {
            while (*a != '\0' && *a == *b) {
                ++a;
                ++b;
            }
            return *a == *b;
        }

        auto find(const step* steps, usize count, const char* name) -> isize {
            for (usize i = 0; i < count; ++i) {
                if (names_equal(steps[i].name, name)) {
                    return static_cast<isize>(i);
                }
            }
            return -1;
        }

        struct plan {
            const step* steps = nullptr;
            usize       count = 0;
            usize       order[max_steps]{};
            state       status[max_steps]{};
            u8          color[max_steps]{}; // 0 unvisited, 1 on stack, 2 emitted
            usize       emitted = 0;
        };

        // Depth-first post-order: emit every dependency before its dependent.
        // Grey nodes catch cycles.
        auto visit(plan& p, usize index) -> bool {
            if (p.color[index] == 2) {
                return true;
            }
            if (p.color[index] == 1) {
                log_line("init dependency cycle at ", p.steps[index].name);
                return false;
            }
            p.color[index] = 1;

            const step& s = p.steps[index];
            for (const char* const* deps = s.needs; deps != nullptr && *deps != nullptr;
                 ++deps) {
                const isize dep = find(p.steps, p.count, *deps);
                if (dep < 0) {
                    log_line("init unknown dependency ", *deps);
                    return false;
                }
                if (!visit(p, static_cast<usize>(dep))) {
                    return false;
                }
            }
            for (const char* const* deps = s.after; deps != nullptr && *deps != nullptr;
                 ++deps) {
                const isize dep = find(p.steps, p.count, *deps);
                if (dep < 0) {
                    log_line("init unknown ordering ", *deps);
                    return false;
                }
                if (!visit(p, static_cast<usize>(dep))) {
                    return false;
                }
            }

            p.color[index]       = 2;
            p.order[p.emitted++] = index;
            return true;
        }

        // Seed the DFS by phase so unordered steps keep a stable, coarse
        // order; explicit edges override it.
        auto topological(plan& p) -> bool {
            constexpr phase phases[] = {
                phase::early,      phase::memory, phase::devices,
                phase::subsystems, phase::late,
            };
            for (phase level : phases) {
                for (usize i = 0; i < p.count; ++i) {
                    if (p.steps[i].level == level && p.color[i] == 0) {
                        if (!visit(p, i)) {
                            return false;
                        }
                    }
                }
            }
            return true;
        }

        auto execute(plan& p, context& ctx) -> bool {
            bool failed = false;
            for (usize k = 0; k < p.emitted; ++k) {
                const usize index = p.order[k];
                const step& s     = p.steps[index];

                bool missing = false;
                for (const char* const* deps = s.needs;
                     deps != nullptr && *deps != nullptr; ++deps) {
                    const isize dep = find(p.steps, p.count, *deps);
                    if (dep < 0 || p.status[static_cast<usize>(dep)] != state::ok) {
                        missing = true;
                        break;
                    }
                }

                if (missing) {
                    p.status[index] = state::skipped;
                    log_line("init skip ", s.name);
                    if (!s.optional) {
                        failed = true;
                    }
                    continue;
                }

                log_line("init ", s.name);
                const bool ok   = s.fn != nullptr && s.fn(ctx);
                p.status[index] = ok ? state::ok : state::failed;
                if (!ok) {
                    log_line("init FAILED ", s.name);
                    if (!s.optional) {
                        failed = true;
                    }
                }
            }
            return !failed;
        }

    } // namespace

    auto run_steps(const step* first, const step* last, context& ctx) -> bool {
        const usize count = static_cast<usize>(last - first);
        if (count == 0) {
            return true;
        }
        if (count > max_steps) {
            astia::log::error("init too many steps");
            return false;
        }

        plan p;
        p.steps = first;
        p.count = count;

        if (!topological(p)) {
            return false;
        }
        return execute(p, ctx);
    }

#ifndef ASTIA_HOST_TEST
    // Populated by the linker script around the .initcall input sections.
    extern "C" const step __init_steps_start[];
    extern "C" const step __init_steps_end[];

    auto run(context& ctx) -> bool {
        const usize count = static_cast<usize>(__init_steps_end - __init_steps_start);
        if (count == 0) {
            astia::log::error("init no steps registered");
            return true;
        }
        return run_steps(__init_steps_start, __init_steps_end, ctx);
    }
#endif

} // namespace astia::init
