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
    ASTIA_CHECK(astia::string_equal({embedded, sizeof(embedded)}, {same, sizeof(same)}));
    ASTIA_CHECK(
        !astia::string_equal({embedded, sizeof(embedded)}, {shorter, sizeof(shorter)}));
    ASTIA_CHECK(astia::string_equal({}, {nullptr, 0}));
    ASTIA_CHECK(!astia::string_equal({nullptr, 1}, {nullptr, 1}));
    ASTIA_CHECK(!astia::string_starts_with({nullptr, 1}, {}));
}

ASTIA_TEST(c_string_equal_compares_nul_terminated_bytes) {
    ASTIA_CHECK(astia::string_equal("path", "path"));
    ASTIA_CHECK(!astia::string_equal("path", "pata"));
    ASTIA_CHECK(!astia::string_equal("path", "pat"));
    ASTIA_CHECK(!astia::string_equal("pat", "path"));
    ASTIA_CHECK(astia::string_equal("", ""));
    ASTIA_CHECK(!astia::string_equal("", "x"));
    ASTIA_CHECK(!astia::string_equal("x", ""));
    ASTIA_CHECK(!astia::string_equal(nullptr, "x"));
    ASTIA_CHECK(!astia::string_equal("x", nullptr));
    ASTIA_CHECK(!astia::string_equal(nullptr, nullptr));

    // The C-string and string_view overloads stay callable side by side.
    const char value[] = "path";
    ASTIA_CHECK(
        astia::string_equal({value, sizeof(value) - 1}, {value, sizeof(value) - 1}));
}

ASTIA_TEST(string_view_prefix_and_ordering) {
    const char value[]  = "kernel";
    const char prefix[] = "ker";
    const char longer[] = "kernels";
    ASTIA_CHECK(astia::string_starts_with({value, sizeof(value) - 1},
                                          {prefix, sizeof(prefix) - 1}));
    ASTIA_CHECK(astia::string_starts_with({}, {}));
    ASTIA_CHECK(!astia::string_starts_with({prefix, sizeof(prefix) - 1},
                                           {value, sizeof(value) - 1}));
    ASTIA_CHECK(astia::string_compare({value, sizeof(value) - 1},
                                      {longer, sizeof(longer) - 1}) < 0);
    ASTIA_CHECK(astia::string_compare({longer, sizeof(longer) - 1},
                                      {value, sizeof(value) - 1}) > 0);
    ASTIA_CHECK(astia::string_compare({nullptr, 1}, {}) < 0);
}

ASTIA_TEST(string_before_nul_respects_range_boundary) {
    const char         no_terminator[] = {'x', 'y'};
    const char         terminated[]    = {'x', '\0', 'y'};
    astia::string_view result;
    ASTIA_CHECK(
        !astia::string_before_nul({no_terminator, sizeof(no_terminator)}, result));
    ASTIA_CHECK(astia::string_before_nul({terminated, sizeof(terminated)}, result));
    ASTIA_CHECK(result.data == terminated && result.size == 1);
    ASTIA_CHECK(!astia::string_before_nul({nullptr, 1}, result));
    ASTIA_CHECK(!astia::string_before_nul({}, result));
}
