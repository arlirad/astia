#include "dl_unload_host.h"
#include "support/test.hpp"

#include <cstring>

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

ASTIA_TEST(dl_unmap_failure_reports_error_and_keeps_retry_token) {
    astia_dl_test_reset();
    astia_dl_test_set_unmap_result(-1);

    const int module = astia_dl_test_add_module(module_base, module_span, 1, 0);
    const int deps[] = {module};

    void* handle = astia_dl_test_add_handle(module, deps, 1);

    ASTIA_CHECK(astia_dl_test_close(handle) == -1);
    ASTIA_CHECK(astia_dl_test_handle_live(handle));
    ASTIA_CHECK(astia_dl_test_module_used(module));
    ASTIA_CHECK(astia_dl_test_module_pending(module));
    ASTIA_CHECK(astia_dl_test_unmap_count() == 1);

    const char* error = astia_dl_test_error();

    ASTIA_CHECK(error != nullptr);
    ASTIA_CHECK(error != nullptr && std::strstr(error, "unmap") != nullptr);

    astia_dl_test_set_unmap_result(0);

    ASTIA_CHECK(astia_dl_test_close(handle) == 0);
    ASTIA_CHECK(!astia_dl_test_handle_live(handle));
    ASTIA_CHECK(!astia_dl_test_module_used(module));
    ASTIA_CHECK(!astia_dl_test_module_pending(module));
    ASTIA_CHECK(astia_dl_test_unmap_count() == 2);
}

ASTIA_TEST(dl_deferred_retry_reports_unmap_failure) {
    astia_dl_test_reset();
    astia_dl_test_set_threads(2);

    const int module = astia_dl_test_add_module(module_base, module_span, 1, 0);
    const int deps[] = {module};

    void* handle = astia_dl_test_add_handle(module, deps, 1);

    ASTIA_CHECK(astia_dl_test_close(handle) == 0);
    ASTIA_CHECK(astia_dl_test_handle_live(handle));
    ASTIA_CHECK(astia_dl_test_module_pending(module));
    ASTIA_CHECK(astia_dl_test_error() == nullptr);

    astia_dl_test_set_threads(1);
    astia_dl_test_set_unmap_result(-1);

    ASTIA_CHECK(astia_dl_test_close(handle) == -1);
    ASTIA_CHECK(astia_dl_test_handle_live(handle));
    ASTIA_CHECK(astia_dl_test_module_used(module));
    ASTIA_CHECK(astia_dl_test_module_pending(module));
    ASTIA_CHECK(astia_dl_test_unmap_count() == 1);

    const char* error = astia_dl_test_error();

    ASTIA_CHECK(error != nullptr);
    ASTIA_CHECK(error != nullptr && std::strstr(error, "unmap") != nullptr);

    astia_dl_test_set_unmap_result(0);

    ASTIA_CHECK(astia_dl_test_close(handle) == 0);
    ASTIA_CHECK(!astia_dl_test_handle_live(handle));
    ASTIA_CHECK(!astia_dl_test_module_used(module));
    ASTIA_CHECK(!astia_dl_test_module_pending(module));
    ASTIA_CHECK(astia_dl_test_unmap_count() == 2);
}

ASTIA_TEST(dl_shared_module_refcount_retains_until_last_handle) {
    astia_dl_test_reset();
    astia_dl_test_set_threads(2);

    const int module = astia_dl_test_add_module(module_base, module_span, 2, 0);
    const int deps[] = {module};

    void* first  = astia_dl_test_add_handle(module, deps, 1);
    void* second = astia_dl_test_add_handle(module, deps, 1);

    ASTIA_CHECK(astia_dl_test_close(first) == 0);
    ASTIA_CHECK(!astia_dl_test_handle_live(first));
    ASTIA_CHECK(astia_dl_test_module_used(module));
    ASTIA_CHECK(astia_dl_test_module_refcount(module) == 1);
    ASTIA_CHECK(!astia_dl_test_module_pending(module));
    ASTIA_CHECK(astia_dl_test_unmap_count() == 0);

    ASTIA_CHECK(astia_dl_test_close(second) == 0);
    ASTIA_CHECK(astia_dl_test_handle_live(second));
    ASTIA_CHECK(astia_dl_test_module_used(module));
    ASTIA_CHECK(astia_dl_test_module_refcount(module) == 0);
    ASTIA_CHECK(astia_dl_test_module_pending(module));
    ASTIA_CHECK(astia_dl_test_unmap_count() == 0);

    ASTIA_CHECK(astia_dl_test_close(second) == 0);
    ASTIA_CHECK(astia_dl_test_handle_live(second));
    ASTIA_CHECK(astia_dl_test_module_pending(module));
    ASTIA_CHECK(astia_dl_test_unmap_count() == 0);

    astia_dl_test_set_threads(1);

    ASTIA_CHECK(astia_dl_test_close(second) == 0);
    ASTIA_CHECK(!astia_dl_test_handle_live(second));
    ASTIA_CHECK(!astia_dl_test_module_used(module));
    ASTIA_CHECK(!astia_dl_test_module_pending(module));
    ASTIA_CHECK(astia_dl_test_unmap_count() == 1);
}

ASTIA_TEST(dl_nodelete_module_stays_mapped_and_releases_handle) {
    astia_dl_test_reset();
    astia_dl_test_set_threads(2);

    const int module = astia_dl_test_add_module(module_base, module_span, 1, 1);
    const int deps[] = {module};

    void* handle = astia_dl_test_add_handle(module, deps, 1);

    ASTIA_CHECK(astia_dl_test_close(handle) == 0);
    ASTIA_CHECK(!astia_dl_test_handle_live(handle));
    ASTIA_CHECK(astia_dl_test_module_used(module));
    ASTIA_CHECK(astia_dl_test_module_refcount(module) == 0);
    ASTIA_CHECK(!astia_dl_test_module_pending(module));
    ASTIA_CHECK(astia_dl_test_unmap_count() == 0);

    astia_dl_test_set_threads(1);

    ASTIA_CHECK(astia_dl_test_close(handle) == -1);
    ASTIA_CHECK(astia_dl_test_module_used(module));
    ASTIA_CHECK(astia_dl_test_unmap_count() == 0);
}

ASTIA_TEST(dl_group_unmaps_mortal_module_and_keeps_nodelete) {
    astia_dl_test_reset();

    const int mortal = astia_dl_test_add_module(module_base, module_span, 1, 0);
    const int retained =
        astia_dl_test_add_module(module_base + module_span, module_span, 1, 1);
    const int deps[] = {mortal, retained};

    void* handle = astia_dl_test_add_handle(retained, deps, 2);

    ASTIA_CHECK(astia_dl_test_close(handle) == 0);
    ASTIA_CHECK(!astia_dl_test_handle_live(handle));
    ASTIA_CHECK(!astia_dl_test_module_used(mortal));
    ASTIA_CHECK(!astia_dl_test_module_pending(mortal));
    ASTIA_CHECK(astia_dl_test_module_used(retained));
    ASTIA_CHECK(astia_dl_test_module_refcount(retained) == 0);
    ASTIA_CHECK(!astia_dl_test_module_pending(retained));
    ASTIA_CHECK(astia_dl_test_unmap_count() == 1);

    ASTIA_CHECK(astia_dl_test_close(handle) == -1);
}

ASTIA_TEST(dl_installed_signal_handler_defers_unload) {
    astia_dl_test_reset();
    astia_dl_test_set_signal_handler(0x1234);

    const int module = astia_dl_test_add_module(module_base, module_span, 1, 0);
    const int deps[] = {module};

    void* handle = astia_dl_test_add_handle(module, deps, 1);

    ASTIA_CHECK(astia_dl_test_close(handle) == 0);
    ASTIA_CHECK(astia_dl_test_handle_live(handle));
    ASTIA_CHECK(astia_dl_test_module_used(module));
    ASTIA_CHECK(astia_dl_test_module_pending(module));
    ASTIA_CHECK(astia_dl_test_unmap_count() == 0);

    astia_dl_test_set_signal_handler(0);

    ASTIA_CHECK(astia_dl_test_close(handle) == 0);
    ASTIA_CHECK(!astia_dl_test_handle_live(handle));
    ASTIA_CHECK(!astia_dl_test_module_used(module));
    ASTIA_CHECK(!astia_dl_test_module_pending(module));
    ASTIA_CHECK(astia_dl_test_unmap_count() == 1);
}

ASTIA_TEST(dl_ignored_signal_handler_does_not_defer_unload) {
    astia_dl_test_reset();
    astia_dl_test_set_signal_handler(1);

    const int module = astia_dl_test_add_module(module_base, module_span, 1, 0);
    const int deps[] = {module};

    void* handle = astia_dl_test_add_handle(module, deps, 1);

    ASTIA_CHECK(astia_dl_test_close(handle) == 0);
    ASTIA_CHECK(!astia_dl_test_handle_live(handle));
    ASTIA_CHECK(!astia_dl_test_module_used(module));
    ASTIA_CHECK(!astia_dl_test_module_pending(module));
    ASTIA_CHECK(astia_dl_test_unmap_count() == 1);
}
