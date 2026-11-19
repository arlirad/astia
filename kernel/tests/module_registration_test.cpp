#include "../src/mod/module_registration.hpp"
#include "support/test.hpp"

namespace {

    struct slot {
        void*       instance = nullptr;
        const void* ops      = nullptr;
    };

}

ASTIA_TEST(module_registration_rolls_back_append_but_not_reuse) {
    slot       slots[3]{};
    astia::u32 count  = 2;
    slots[0].instance = reinterpret_cast<void*>(1);
    slots[0].ops      = reinterpret_cast<const void*>(1);
    slots[1].instance = reinterpret_cast<void*>(2);
    slots[1].ops      = reinterpret_cast<const void*>(2);
    slots[0]          = {};

    const auto reused = astia::mod::detail::acquire_registration_slot(slots, count, 3);
    ASTIA_CHECK(reused.index == 0);
    ASTIA_CHECK(!reused.appended);
    ASTIA_CHECK(count == 2);
    astia::mod::detail::rollback_registration_slot(count, reused);
    ASTIA_CHECK(count == 2);

    const auto reprobe = astia::mod::detail::acquire_registration_slot(slots, count, 3);
    ASTIA_CHECK(reprobe.index == 0);
    ASTIA_CHECK(!reprobe.appended);
    ASTIA_CHECK(count == 2);

    slots[0]          = {};
    slots[1]          = {};
    count             = 1;
    slots[0].instance = reinterpret_cast<void*>(1);
    slots[0].ops      = reinterpret_cast<const void*>(1);

    const auto appended = astia::mod::detail::acquire_registration_slot(slots, count, 3);
    ASTIA_CHECK(appended.index == 1);
    ASTIA_CHECK(appended.appended);
    ASTIA_CHECK(count == 2);
    astia::mod::detail::rollback_registration_slot(count, appended);
    ASTIA_CHECK(count == 1);

    const auto append_reprobe =
        astia::mod::detail::acquire_registration_slot(slots, count, 3);
    ASTIA_CHECK(append_reprobe.index == 1);
    ASTIA_CHECK(append_reprobe.appended);
    ASTIA_CHECK(count == 2);
}
