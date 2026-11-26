#include <astia/core/name.hpp>

#include "support/test.hpp"

#include <cstring>

namespace {
    bool equals(const char* text, const char* expected) {
        return std::strcmp(text, expected) == 0;
    }
} // namespace

ASTIA_TEST(device_name_letter_increment) {
    char name[32]{};

    ASTIA_CHECK(astia::core::name_letter_increment(name, sizeof name, "sd%"));
    ASTIA_CHECK(equals(name, "sda"));

    ASTIA_CHECK(astia::core::name_letter_increment(name, sizeof name, "sd%"));
    ASTIA_CHECK(equals(name, "sdb"));
}

ASTIA_TEST(device_name_letter_carries_past_z) {
    char name[32]{};

    for (int i = 0; i < 26; ++i) {
        ASTIA_CHECK(astia::core::name_letter_increment(name, sizeof name, "wrap%"));
        ASTIA_CHECK(static_cast<unsigned char>(name[4]) == 'a' + i);
    }

    ASTIA_CHECK(astia::core::name_letter_increment(name, sizeof name, "wrap%"));
    ASTIA_CHECK(equals(name, "wrapaa"));
}

ASTIA_TEST(device_name_number_increment) {
    char name[32]{};

    ASTIA_CHECK(astia::core::name_number_increment(name, sizeof name, "tty%"));
    ASTIA_CHECK(equals(name, "tty0"));

    for (int i = 1; i < 10; ++i) {
        ASTIA_CHECK(astia::core::name_number_increment(name, sizeof name, "tty%"));
    }

    ASTIA_CHECK(equals(name, "tty9"));
    ASTIA_CHECK(astia::core::name_number_increment(name, sizeof name, "tty%"));
    ASTIA_CHECK(equals(name, "tty10"));
}

ASTIA_TEST(device_name_keeps_prefix_and_suffix) {
    char name[32]{};

    ASTIA_CHECK(astia::core::name_letter_increment(name, sizeof name, "x%y"));
    ASTIA_CHECK(equals(name, "xay"));

    ASTIA_CHECK(astia::core::name_number_increment(name, sizeof name, "a%z"));
    ASTIA_CHECK(equals(name, "a0z"));
}

ASTIA_TEST(device_name_rejects_malformed_patterns) {
    char name[32]{};

    ASTIA_CHECK(!astia::core::name_letter_increment(name, sizeof name, "sda"));
    ASTIA_CHECK(!astia::core::name_letter_increment(name, sizeof name, "a%b%c"));
    ASTIA_CHECK(!astia::core::name_letter_increment(name, sizeof name, nullptr));
    ASTIA_CHECK(!astia::core::name_number_increment(nullptr, 8, "tty%"));
}

ASTIA_TEST(device_name_capacity_failure_does_not_advance) {
    char name[16]{};

    ASTIA_CHECK(!astia::core::name_letter_increment(name, 5, "disk%"));
    ASTIA_CHECK(astia::core::name_letter_increment(name, sizeof name, "disk%"));
    ASTIA_CHECK(equals(name, "diska"));
}

ASTIA_TEST(device_name_block_patterns) {
    char name[32]{};

    ASTIA_CHECK(astia::core::name_number_increment(name, sizeof name, "/dev/vblk%"));
    ASTIA_CHECK(equals(name, "/dev/vblk0"));

    ASTIA_CHECK(astia::core::name_number_increment(name, sizeof name, "/dev/vblk%"));
    ASTIA_CHECK(equals(name, "/dev/vblk1"));

    ASTIA_CHECK(astia::core::name_number_increment(name, sizeof name, "/dev/nvme%"));
    ASTIA_CHECK(equals(name, "/dev/nvme0"));
}
