#include <astia/memory.hpp>
#include <astia/strings.hpp>

#include "test.hpp"

ASTIA_TEST(align_up_rounds_up) {
    ASTIA_CHECK(astia::align_up(0u, 4096u) == 0u);
    ASTIA_CHECK(astia::align_up(1u, 4096u) == 4096u);
    ASTIA_CHECK(astia::align_up(4096u, 4096u) == 4096u);
    ASTIA_CHECK(astia::align_up(4097u, 4096u) == 8192u);
    ASTIA_CHECK(astia::align_up(0x10b000ull, 4096) == 0x10b000ull);
    ASTIA_CHECK(astia::align_up(0x10b001ull, 4096) == 0x10c000ull);
}

ASTIA_TEST(align_down_rounds_down) {
    ASTIA_CHECK(astia::align_down(0x10b000ull, 4096) == 0x10b000ull);
    ASTIA_CHECK(astia::align_down(0x10bfffull, 4096) == 0x10b000ull);
    ASTIA_CHECK(astia::align_down(5u, 4u) == 4u);
}

ASTIA_TEST(fill_zero_clears_buffer) {
    unsigned char buffer[8];

    for (auto& byte : buffer) {
        byte = 0xff;
    }

    astia::fill_zero(buffer, sizeof(buffer));

    for (auto byte : buffer) {
        ASTIA_CHECK(byte == 0);
    }
}

ASTIA_TEST(memset_fills_and_nul_terminates) {
    char buffer[4] = {};

    memset(buffer, 'a', 3);
    ASTIA_CHECK(buffer[0] == 'a' && buffer[1] == 'a' && buffer[2] == 'a');
    ASTIA_CHECK(buffer[3] == 0);
}

ASTIA_TEST(memcpy_and_memcmp) {
    const char source[5] = {'a', 'b', 'c', 'd', 'e'};
    char       copy[5]   = {};

    memcpy(copy, source, sizeof(source));
    ASTIA_CHECK(memcmp(source, copy, sizeof(source)) == 0);

    copy[2] = 'z';

    ASTIA_CHECK(memcmp(source, copy, sizeof(source)) != 0);
    ASTIA_CHECK(memcmp(source, copy, 2) == 0);
}

ASTIA_TEST(memmove_handles_overlap) {
    char forward[8] = {'0', '1', '2', '3', '4', '5', '6', '7'};

    memmove(forward + 2, forward, 6);
    ASTIA_CHECK(forward[2] == '0' && forward[7] == '5');

    char backward[8] = {'0', '1', '2', '3', '4', '5', '6', '7'};

    memmove(backward, backward + 2, 6);
    ASTIA_CHECK(backward[0] == '2' && backward[5] == '7');
}

ASTIA_TEST(string_views_are_length_delimited) {
    const char embedded[] = {'a', '\0', 'b'};
    const char same[]     = {'a', '\0', 'b'};
    const char shorter[]  = {'a', '\0'};

    const astia::string_view embedded_view{embedded, sizeof(embedded)};
    const astia::string_view same_view{same, sizeof(same)};
    const astia::string_view shorter_view{shorter, sizeof(shorter)};
    const astia::string_view invalid_view{nullptr, 1};

    ASTIA_CHECK(embedded_view.equals(same_view));
    ASTIA_CHECK(!embedded_view.equals(shorter_view));
    ASTIA_CHECK(astia::string_view{}.equals({nullptr, 0}));
    ASTIA_CHECK(!invalid_view.equals(invalid_view));
    ASTIA_CHECK(!invalid_view.starts_with({}));
}

ASTIA_TEST(c_string_views_are_constructed_explicitly) {
    const char value[] = "path";

    const astia::string_view value_view = astia::string_view::from_c_str(value);

    ASTIA_CHECK(value_view.equals({value, sizeof(value) - 1}));
    ASTIA_CHECK(astia::string_view::from_c_str("").empty());
    ASTIA_CHECK(astia::string_view::from_c_str("").valid());
    ASTIA_CHECK(astia::string_view::from_c_str(nullptr).valid());
    ASTIA_CHECK(astia::string_view::from_c_str(nullptr).empty());
}

ASTIA_TEST(string_view_prefix_and_ordering) {
    const char value[]  = "kernel";
    const char prefix[] = "ker";
    const char longer[] = "kernels";

    const astia::string_view value_view{value, sizeof(value) - 1};
    const astia::string_view prefix_view{prefix, sizeof(prefix) - 1};
    const astia::string_view longer_view{longer, sizeof(longer) - 1};
    const astia::string_view invalid_view{nullptr, 1};

    ASTIA_CHECK(value_view.starts_with(prefix_view));
    ASTIA_CHECK(astia::string_view{}.starts_with({}));
    ASTIA_CHECK(!prefix_view.starts_with(value_view));
    ASTIA_CHECK(value_view.compare(longer_view) < 0);
    ASTIA_CHECK(longer_view.compare(value_view) > 0);
    ASTIA_CHECK(invalid_view.compare({}) < 0);
}

ASTIA_TEST(string_view_find_nul_respects_range_boundary) {
    const char no_terminator[] = {'x', 'y'};
    const char terminated[]    = {'x', '\0', 'y'};

    const astia::string_view no_terminator_view{no_terminator, sizeof(no_terminator)};
    const astia::string_view terminated_view{terminated, sizeof(terminated)};
    const astia::string_view invalid_view{nullptr, 1};

    astia::string_view result;

    ASTIA_CHECK(!no_terminator_view.find_nul(result));
    ASTIA_CHECK(terminated_view.find_nul(result));
    ASTIA_CHECK(result.data == terminated && result.size == 1);
    ASTIA_CHECK(!invalid_view.find_nul(result));
    ASTIA_CHECK(!astia::string_view{}.find_nul(result));
}
