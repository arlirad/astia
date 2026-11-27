#include "dl_unload_host.h"
#include "support/test.hpp"

/* ABI mirror of <dlfcn.h>'s struct; the size check in collect() catches any
   drift between this layout and the one the loader writes. */
struct dl_phdr_info {
    uint64_t    dlpi_addr;
    const char* dlpi_name;
    const void* dlpi_phdr;
    uint16_t    dlpi_phnum;
    uint64_t    dlpi_adds;
    uint64_t    dlpi_subs;
    uint64_t    dlpi_tls_modid;
    void*       dlpi_tls_data;
};

namespace {
    constexpr uint64_t first_base  = 0x700000000000ULL;
    constexpr uint64_t second_base = 0x710000000000ULL;
    constexpr uint64_t third_base  = 0x720000000000ULL;
    constexpr uint64_t module_span = 0x1000;

    struct visit_state {
        int          recorded      = 0;
        int          capacity      = 0;
        bool         size_mismatch = false;
        dl_phdr_info records[4]    = {};
    };

    int collect(dl_phdr_info* info, size_t size, void* data) {
        auto* state = static_cast<visit_state*>(data);

        if (size != sizeof(dl_phdr_info))
            state->size_mismatch = true;

        if (state->recorded < state->capacity)
            state->records[state->recorded] = *info;

        ++state->recorded;

        return 0;
    }

    int stop_after_two(dl_phdr_info*, size_t, void* data) {
        auto* visited = static_cast<int*>(data);

        ++*visited;

        return *visited == 2 ? 1 : 0;
    }

    visit_state walk() {
        visit_state state;

        state.capacity = 4;

        ASTIA_CHECK(astia_dl_test_iterate(collect, &state) == 0);

        return state;
    }
} // namespace

ASTIA_TEST(dl_iterate_phdr_reports_adds_subs_and_tls_modid) {
    astia_dl_test_reset();

    astia_dl_test_add_module(first_base, module_span, 1, 0);

    const int tls = astia_dl_test_add_module(second_base, module_span, 1, 0);

    astia_dl_test_module_set_tls(tls, 64);

    const visit_state state = walk();

    ASTIA_CHECK(state.recorded == 2);
    ASTIA_CHECK(!state.size_mismatch);
    ASTIA_CHECK(state.records[0].dlpi_addr == first_base);
    ASTIA_CHECK(state.records[1].dlpi_addr == second_base);
    ASTIA_CHECK(state.records[0].dlpi_adds == 2);
    ASTIA_CHECK(state.records[1].dlpi_adds == 2);
    ASTIA_CHECK(state.records[0].dlpi_subs == 0);
    ASTIA_CHECK(state.records[1].dlpi_subs == 0);
    ASTIA_CHECK(state.records[0].dlpi_tls_modid == 0);
    ASTIA_CHECK(state.records[1].dlpi_tls_modid == static_cast<uint64_t>(tls + 1));
    ASTIA_CHECK(state.records[0].dlpi_tls_data == nullptr);
    ASTIA_CHECK(state.records[1].dlpi_tls_data == nullptr);
}

ASTIA_TEST(dl_iterate_phdr_counts_removals_but_not_deferred_unloads) {
    astia_dl_test_reset();
    astia_dl_test_set_threads(2);

    astia_dl_test_add_module(first_base, module_span, 1, 0);

    const int doomed = astia_dl_test_add_module(second_base, module_span, 1, 0);
    const int deps[] = {doomed};

    void* handle = astia_dl_test_add_handle(doomed, deps, 1);

    ASTIA_CHECK(astia_dl_test_close(handle) == 0);
    ASTIA_CHECK(astia_dl_test_module_pending(doomed));

    /* A deferred unload keeps the image mapped and the removal count still. */
    visit_state deferred = walk();

    ASTIA_CHECK(deferred.recorded == 2);
    ASTIA_CHECK(deferred.records[1].dlpi_addr == second_base);
    ASTIA_CHECK(deferred.records[0].dlpi_adds == 2);
    ASTIA_CHECK(deferred.records[0].dlpi_subs == 0);

    astia_dl_test_set_threads(1);

    ASTIA_CHECK(astia_dl_test_close(handle) == 0);
    ASTIA_CHECK(!astia_dl_test_module_used(doomed));

    const visit_state unloaded = walk();

    ASTIA_CHECK(unloaded.recorded == 1);
    ASTIA_CHECK(unloaded.records[0].dlpi_addr == first_base);
    ASTIA_CHECK(unloaded.records[0].dlpi_adds == 2);
    ASTIA_CHECK(unloaded.records[0].dlpi_subs == 1);
}

ASTIA_TEST(dl_iterate_phdr_propagates_callback_stop) {
    astia_dl_test_reset();

    astia_dl_test_add_module(first_base, module_span, 1, 0);
    astia_dl_test_add_module(second_base, module_span, 1, 0);
    astia_dl_test_add_module(third_base, module_span, 1, 0);

    int visited = 0;

    ASTIA_CHECK(astia_dl_test_iterate(stop_after_two, &visited) == 1);
    ASTIA_CHECK(visited == 2);
}

ASTIA_TEST(dl_iterate_phdr_null_callback_is_a_noop) {
    astia_dl_test_reset();

    astia_dl_test_add_module(first_base, module_span, 1, 0);

    ASTIA_CHECK(astia_dl_test_iterate(nullptr, nullptr) == 0);
}
