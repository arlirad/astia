#include <astia/mm/frame_allocator.hpp>
#include <astia/mm/heap.hpp>
#include <astia/strings.hpp>

#include "arena.hpp"
#include "heap_exhaustion.hpp"
#include "test.hpp"

namespace {
    using astia::error_t;

    // The heap backs its window through map_fn; on the host the window is
    // plain memory that is already reachable, so the arch hook is a no-op.
    bool fake_map(astia::u64, astia::u64) {
        return true;
    }

    alignas(16) astia::u8 region[64 * 1024];

    // Configure the heap once, backed by the fixed arena like the other
    // substrate-bound tests.
    void init_heap() {
        astia_test::map_arena();

        static bool ready = false;
        if (ready)
            return;

        astia::mm::memory_map map;

        map.add(astia_test::arena_base, astia_test::arena_size,
                astia::mm::memory_type::usable);
        astia::mm::kernel_frames().init(map);
        astia::mm::heap::configure(region, sizeof(region), fake_map);
        astia::mm::heap::init();

        ready = true;
    }
} // namespace

ASTIA_TEST(string_default_is_empty_and_non_owning) {
    init_heap();

    astia::strings::string value;

    ASTIA_CHECK(value.empty());
    ASTIA_CHECK(value.size() == 0);
    ASTIA_CHECK(value.c_str() != nullptr);
    ASTIA_CHECK(value.c_str()[0] == '\0');
    ASTIA_CHECK(value.view().valid());
    ASTIA_CHECK(value.view().data == nullptr && value.view().size == 0);
}

ASTIA_TEST(string_try_from_c_str_copies) {
    init_heap();

    auto value = astia::strings::string::try_from("hello");

    ASTIA_CHECK(value);
    ASTIA_CHECK(value.value.size() == 5);
    ASTIA_CHECK(value.value.c_str()[5] == '\0');
    ASTIA_CHECK(astia::string_view::from_c_str(value.value.c_str()).equals({"hello", 5}));
    ASTIA_CHECK(value.value.view().equals({"hello", 5}));
}

ASTIA_TEST(string_preserves_embedded_nuls) {
    init_heap();

    const char raw[] = {'a', '\0', 'b'};

    auto value = astia::strings::string::try_from(astia::string_view{raw, sizeof(raw)});

    ASTIA_CHECK(value);
    ASTIA_CHECK(value.value.size() == sizeof(raw));
    ASTIA_CHECK(value.value.view().equals({raw, sizeof(raw)}));
    ASTIA_CHECK(value.value.c_str()[1] == '\0');
    ASTIA_CHECK(value.value.c_str()[3] == '\0');
}

ASTIA_TEST(string_try_from_rejects_invalid_input) {
    init_heap();

    auto from_null = astia::strings::string::try_from(static_cast<const char*>(nullptr));

    ASTIA_CHECK(!from_null);
    ASTIA_CHECK(from_null.error == error_t::einval);

    auto from_invalid = astia::strings::string::try_from(astia::string_view{nullptr, 1});

    ASTIA_CHECK(!from_invalid);
    ASTIA_CHECK(from_invalid.error == error_t::einval);
}

ASTIA_TEST(string_assign_replaces_contents) {
    init_heap();

    auto value = astia::strings::string::try_from("alpha");

    ASTIA_CHECK(value);
    ASTIA_CHECK(value.value.assign({"beta", 4}));
    ASTIA_CHECK(value.value.view().equals({"beta", 4}));

    ASTIA_CHECK(value.value.assign({}));
    ASTIA_CHECK(value.value.empty());
    ASTIA_CHECK(value.value.view().data == nullptr);
    ASTIA_CHECK(value.value.c_str()[0] == '\0');

    ASTIA_CHECK(!value.value.assign({nullptr, 1}));
    ASTIA_CHECK(value.value.empty());
}

ASTIA_TEST(string_append_grows_and_keeps_terminator) {
    init_heap();

    auto value = astia::strings::string::try_from("a");

    ASTIA_CHECK(value);
    ASTIA_CHECK(value.value.append({"bc", 2}));
    ASTIA_CHECK(value.value.view().equals({"abc", 3}));
    ASTIA_CHECK(value.value.c_str()[3] == '\0');

    ASTIA_CHECK(value.value.append({}));
    ASTIA_CHECK(value.value.view().equals({"abc", 3}));

    ASTIA_CHECK(!value.value.append({nullptr, 1}));
    ASTIA_CHECK(value.value.view().equals({"abc", 3}));
}

ASTIA_TEST(string_clone_is_independent) {
    init_heap();

    auto original = astia::strings::string::try_from("clone");

    ASTIA_CHECK(original);

    auto copy = original.value.clone();

    ASTIA_CHECK(copy);
    ASTIA_CHECK(copy.value.view().equals(original.value.view()));

    ASTIA_CHECK(original.value.assign({"other", 5}));
    ASTIA_CHECK(copy.value.view().equals({"clone", 5}));
    ASTIA_CHECK(original.value.view().equals({"other", 5}));
}

ASTIA_TEST(string_move_transfers_ownership) {
    init_heap();

    auto source = astia::strings::string::try_from("move");

    ASTIA_CHECK(source);

    astia::strings::string moved(static_cast<astia::strings::string&&>(source.value));

    ASTIA_CHECK(moved.view().equals({"move", 4}));
    ASTIA_CHECK(source.value.empty());
    ASTIA_CHECK(source.value.view().data == nullptr);

    astia::strings::string target;

    target = static_cast<astia::strings::string&&>(moved);

    ASTIA_CHECK(target.view().equals({"move", 4}));
    ASTIA_CHECK(moved.empty());
}

ASTIA_TEST(string_self_move_is_safe) {
    init_heap();

    auto value = astia::strings::string::try_from("self");

    ASTIA_CHECK(value);

    astia::strings::string& alias = value.value;

    alias = static_cast<astia::strings::string&&>(alias);

    ASTIA_CHECK(alias.view().equals({"self", 4}));
}

ASTIA_TEST(string_clear_keeps_the_value_usable) {
    init_heap();

    auto value = astia::strings::string::try_from("reusable");

    ASTIA_CHECK(value);
    value.value.clear();
    ASTIA_CHECK(value.value.empty());
    ASTIA_CHECK(value.value.c_str()[0] == '\0');

    ASTIA_CHECK(value.value.assign({"again", 5}));
    ASTIA_CHECK(value.value.view().equals({"again", 5}));
}

ASTIA_TEST(string_try_from_reports_oom) {
    init_heap();

    astia_test::heap_exhaustion exhaustion;

    ASTIA_CHECK(exhaustion.acquire());

    auto value = astia::strings::string::try_from("oom");

    ASTIA_CHECK(!value);
    ASTIA_CHECK(value.error == error_t::enomem);
}

ASTIA_TEST(string_failed_mutation_preserves_contents) {
    init_heap();

    auto value = astia::strings::string::try_from("keep");

    ASTIA_CHECK(value);

    {
        astia_test::heap_exhaustion exhaustion;

        ASTIA_CHECK(exhaustion.acquire());
        ASTIA_CHECK(!value.value.assign({"longer", 6}));
        ASTIA_CHECK(value.value.view().equals({"keep", 4}));
        ASTIA_CHECK(!value.value.append({"tail", 4}));
        ASTIA_CHECK(value.value.view().equals({"keep", 4}));
    }

    ASTIA_CHECK(value.value.append({"!", 1}));
    ASTIA_CHECK(value.value.view().equals({"keep!", 5}));
}

ASTIA_TEST(string_clear_keeps_capacity_without_heap) {
    init_heap();

    auto value = astia::strings::string::try_from("reusable");

    ASTIA_CHECK(value);
    value.value.clear();

    {
        astia_test::heap_exhaustion exhaustion;

        ASTIA_CHECK(exhaustion.acquire());
        ASTIA_CHECK(value.value.assign({"ok", 2}));
    }

    ASTIA_CHECK(value.value.view().equals({"ok", 2}));
}

ASTIA_TEST(string_append_self_survives_growth) {
    init_heap();

    auto value = astia::strings::string::try_from("ab");

    ASTIA_CHECK(value);
    ASTIA_CHECK(value.value.append(value.value.view()));
    ASTIA_CHECK(value.value.view().equals({"abab", 4}));
    ASTIA_CHECK(value.value.c_str()[4] == '\0');
}
