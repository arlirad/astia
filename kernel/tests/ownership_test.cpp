#include <astia/mm/shptr.hpp>
#include <astia/mm/uptr.hpp>

#include "support/test.hpp"

#include <new>

namespace {
    struct probe {
        int* destroyed;

        ~probe() {
            ++*destroyed;
        }
    };
}

ASTIA_TEST(uptr_moves_and_destroys_once) {
    int destroyed = 0;

    {
        astia::mm::uptr<probe> first(new probe{&destroyed});
        astia::mm::uptr<probe> second(static_cast<astia::mm::uptr<probe>&&>(first));

        ASTIA_CHECK(first == nullptr);
        ASTIA_CHECK(second != nullptr);
        second.reset();
        ASTIA_CHECK(destroyed == 1);
    }

    ASTIA_CHECK(destroyed == 1);
}

ASTIA_TEST(shptr_borrowed_never_deletes) {
    int destroyed = 0;

    probe value{&destroyed};

    {
        auto first  = astia::mm::shptr<probe>::borrowed(&value);
        auto second = first;

        ASTIA_CHECK(first.get() == &value);
        ASTIA_CHECK(second.get() == &value);
    }

    ASTIA_CHECK(destroyed == 0);
}

ASTIA_TEST(shptr_borrowed_static_storage_never_deletes) {
    int destroyed = 0;

    alignas(probe) unsigned char storage[sizeof(probe)]{};
    probe*                       value = ::new (storage) probe{&destroyed};

    {
        auto first  = astia::mm::shptr<probe>::borrowed(value);
        auto second = first;

        ASTIA_CHECK(first.get() == value);
        ASTIA_CHECK(second.get() == value);
    }

    ASTIA_CHECK(destroyed == 0);
    value->~probe();
    ASTIA_CHECK(destroyed == 1);
}

ASTIA_TEST(shptr_adopt_releases_value_when_control_block_allocation_fails) {
    int destroyed = 0;

    probe* value = new probe{&destroyed};

    astia_test::fail_next_allocation();

    auto adopted = astia::mm::shptr<probe>::adopt(
        value, [](void* raw) { delete static_cast<probe*>(raw); });

    ASTIA_CHECK(!adopted);
    ASTIA_CHECK(destroyed == 1);
}
