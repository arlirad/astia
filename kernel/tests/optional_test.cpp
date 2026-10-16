#include <astia/optional.hpp>

#include "test.hpp"

namespace {

    using astia::error_t;
    using astia::optional;

} // namespace

ASTIA_TEST(optional_holds_a_value) {
    optional<int> v = 42;
    ASTIA_CHECK(v);
    ASTIA_CHECK(v.has_value);
    ASTIA_CHECK(v.value == 42);
    ASTIA_CHECK(v.error == error_t::enone);
    ASTIA_CHECK(v.value_or(-1) == 42);
}

ASTIA_TEST(optional_carries_an_error) {
    optional<int> v = error_t::enomem;
    ASTIA_CHECK(!v);
    ASTIA_CHECK(!v.has_value);
    ASTIA_CHECK(v.error == error_t::enomem);
    ASTIA_CHECK(v.value_or(-1) == -1);
}

ASTIA_TEST(optional_default_is_disengaged_but_clean) {
    optional<astia::u64> v;
    ASTIA_CHECK(!v);
    ASTIA_CHECK(v.error == error_t::enone);
    ASTIA_CHECK(v.value_or(7) == 7);
}

ASTIA_TEST(optional_value_with_status) {
    optional<int> v = optional<int>(7, error_t::eagain);
    ASTIA_CHECK(v);
    ASTIA_CHECK(v.value == 7);
    ASTIA_CHECK(v.error == error_t::eagain);
}

ASTIA_TEST(error_string_covers_known_and_unknown) {
    ASTIA_CHECK(astia::error_string(error_t::enone) != nullptr);
    ASTIA_CHECK(astia::error_string(error_t::enomem) != nullptr);
    // Out-of-range codes must still produce something printable.
    ASTIA_CHECK(astia::error_string(static_cast<error_t>(9999)) != nullptr);
}
