#include "dl_unload_host.h"
#include "support/test.hpp"

namespace {
    constexpr uint64_t module_base = 0x700000000000ULL;
    constexpr uint64_t module_span = 0x1000;
} // namespace

ASTIA_TEST(dl_deferred_close_keeps_handle_for_retry) {
    astia_dl_test_reset();
    astia_dl_test_set_threads(2);

    const int module = astia_dl_test_add_module(module_base, module_span, 1, 0);
    const int deps[] = {module};

    void* handle = astia_dl_test_add_handle(module, deps, 1);

    ASTIA_CHECK(astia_dl_test_close(handle) == 0);
    ASTIA_CHECK(astia_dl_test_handle_live(handle));
    ASTIA_CHECK(astia_dl_test_module_used(module));
    ASTIA_CHECK(astia_dl_test_module_pending(module));
    ASTIA_CHECK(astia_dl_test_module_refcount(module) == 0);
    ASTIA_CHECK(astia_dl_test_unmap_count() == 0);

    ASTIA_CHECK(astia_dl_test_close(handle) == 0);
    ASTIA_CHECK(astia_dl_test_handle_live(handle));
    ASTIA_CHECK(astia_dl_test_module_used(module));
    ASTIA_CHECK(astia_dl_test_unmap_count() == 0);

    astia_dl_test_set_threads(1);

    ASTIA_CHECK(astia_dl_test_close(handle) == 0);
    ASTIA_CHECK(!astia_dl_test_handle_live(handle));
    ASTIA_CHECK(!astia_dl_test_module_used(module));
    ASTIA_CHECK(!astia_dl_test_module_pending(module));
    ASTIA_CHECK(astia_dl_test_unmap_count() == 1);

    ASTIA_CHECK(astia_dl_test_close(handle) == -1);
}

ASTIA_TEST(dl_deferred_group_releases_handle_after_every_unmap) {
    astia_dl_test_reset();
    astia_dl_test_set_threads(2);

    const int first = astia_dl_test_add_module(module_base, module_span, 1, 0);
    const int second =
        astia_dl_test_add_module(module_base + module_span, module_span, 1, 0);
    const int deps[] = {first, second};

    void* handle = astia_dl_test_add_handle(second, deps, 2);

    ASTIA_CHECK(astia_dl_test_close(handle) == 0);
    ASTIA_CHECK(astia_dl_test_handle_live(handle));
    ASTIA_CHECK(astia_dl_test_module_pending(first));
    ASTIA_CHECK(astia_dl_test_module_pending(second));
    ASTIA_CHECK(astia_dl_test_unmap_count() == 0);

    astia_dl_test_set_threads(1);

    ASTIA_CHECK(astia_dl_test_close(handle) == 0);
    ASTIA_CHECK(!astia_dl_test_handle_live(handle));
    ASTIA_CHECK(!astia_dl_test_module_used(first));
    ASTIA_CHECK(!astia_dl_test_module_used(second));
    ASTIA_CHECK(astia_dl_test_unmap_count() == 2);
}

ASTIA_TEST(dl_reopened_module_ignores_pending_retry_token) {
    astia_dl_test_reset();
    astia_dl_test_set_threads(2);

    const int module = astia_dl_test_add_module(module_base, module_span, 1, 0);
    const int deps[] = {module};

    void* pending = astia_dl_test_add_handle(module, deps, 1);

    ASTIA_CHECK(astia_dl_test_close(pending) == 0);
    ASTIA_CHECK(astia_dl_test_module_pending(module));

    astia_dl_test_module_reopen(module);

    void* reopened = astia_dl_test_add_handle(module, deps, 1);

    astia_dl_test_set_threads(1);

    ASTIA_CHECK(astia_dl_test_close(reopened) == 0);
    ASTIA_CHECK(!astia_dl_test_handle_live(reopened));
    ASTIA_CHECK(!astia_dl_test_module_used(module));
    ASTIA_CHECK(astia_dl_test_unmap_count() == 1);

    ASTIA_CHECK(astia_dl_test_close(pending) == 0);
    ASTIA_CHECK(!astia_dl_test_handle_live(pending));
    ASTIA_CHECK(astia_dl_test_unmap_count() == 1);
}

ASTIA_TEST(dl_immediate_close_releases_handle) {
    astia_dl_test_reset();

    const int module = astia_dl_test_add_module(module_base, module_span, 1, 0);
    const int deps[] = {module};

    void* handle = astia_dl_test_add_handle(module, deps, 1);

    ASTIA_CHECK(astia_dl_test_close(handle) == 0);
    ASTIA_CHECK(!astia_dl_test_handle_live(handle));
    ASTIA_CHECK(!astia_dl_test_module_used(module));
    ASTIA_CHECK(!astia_dl_test_module_pending(module));
    ASTIA_CHECK(astia_dl_test_unmap_count() == 1);

    ASTIA_CHECK(astia_dl_test_close(handle) == -1);
}
