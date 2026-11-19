#include "support/test.hpp"

#include "../src/mod/module.hpp"

namespace {

    astia::mod::loaded_module* current = nullptr;
    unsigned events[4]{};
    unsigned event_count = 0;
    bool pin_held = false;

    void fini_one() {
        events[event_count++] = 1;
        pin_held = current != nullptr && current->running;
    }

    void fini_two() {
        events[event_count++] = 2;
    }

    void fini_three() {
        events[event_count++] = 3;
    }

}

ASTIA_TEST(module_exit_runs_fini_array_in_reverse_while_pinned) {
    astia::mod::loaded_module module{};
    module.running = true;
    module.initialized = true;
    module.fini_array[0] = fini_one;
    module.fini_array[1] = fini_two;
    module.fini_array[2] = fini_three;
    module.fini_count = 3;
    current = &module;
    event_count = 0;
    pin_held = false;

    ASTIA_CHECK(astia::mod::exit_module(&module));
    ASTIA_CHECK(event_count == 3);
    ASTIA_CHECK(events[0] == 3);
    ASTIA_CHECK(events[1] == 2);
    ASTIA_CHECK(events[2] == 1);
    ASTIA_CHECK(pin_held);
    ASTIA_CHECK(!module.running);
    ASTIA_CHECK(!module.initialized);
    ASTIA_CHECK(!astia::mod::exit_module(&module));
    current = nullptr;
}
