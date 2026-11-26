#pragma once

// Minimal in-repo test framework. Host-side only: these tests build with the
// system toolchain and run as ordinary processes, so there is no dependency
// on QEMU or on the freestanding kernel environment.

#include <cstdio>
#include <vector>

namespace astia_test {
    void fail_next_allocation();

    struct test_case {
        const char* name;
        void (*body)();
    };

    inline std::vector<test_case>& cases() {
        static std::vector<test_case> list;

        return list;
    }

    inline int& failure_count() {
        static int count = 0;

        return count;
    }

    struct registrar {
        registrar(const char* name, void (*body)()) {
            cases().push_back({name, body});
        }
    };

    inline void check(bool ok, const char* expr, const char* file, int line) {
        if (!ok) {
            ++failure_count();
            std::printf("    FAIL %s:%d: %s\n", file, line, expr);
        }
    }

    inline int run_all() {
        for (const auto& test : cases()) {
            const int before = failure_count();

            std::printf("[ RUN  ] %s\n", test.name);
            test.body();
            std::printf("[ %s ] %s\n", failure_count() == before ? " OK " : "FAIL",
                        test.name);
        }

        if (failure_count() == 0) {
            std::printf("all %zu test(s) passed\n", cases().size());

            return 0;
        }

        std::printf("%d check(s) failed\n", failure_count());

        return 1;
    }
} // namespace astia_test

#define ASTIA_TEST(name)                                           \
    static void                    name();                         \
    static ::astia_test::registrar name##_registrar{#name, &name}; \
    static void                    name()

#define ASTIA_CHECK(expr) \
    ::astia_test::check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)
