#include "support/test.hpp"
#include <astia/mm/shptr.hpp>
#include <astia/mm/uptr.hpp>

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
        auto first = astia::mm::shptr<probe>::borrowed(&value);
        auto second = first;
        ASTIA_CHECK(first.get() == &value);
        ASTIA_CHECK(second.get() == &value);
    }
    ASTIA_CHECK(destroyed == 0);
}
