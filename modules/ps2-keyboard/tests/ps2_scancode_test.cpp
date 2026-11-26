#include "scancode.hpp"
#include "support/test.hpp"
#include <initializer_list>

#include <vector>

using astia::ps2::key;
using astia::ps2::key_event;
using astia::ps2::set2_decoder;

namespace {
    std::vector<key_event> decode(std::initializer_list<astia::u8> bytes) {
        set2_decoder           decoder;
        std::vector<key_event> events;

        for (astia::u8 byte : bytes) {
            const key_event event = decoder.feed(byte);

            if (event.valid())
                events.push_back(event);
        }

        return events;
    }
} // namespace

ASTIA_TEST(set2_f1_and_f5_are_not_swapped) {
    auto events = decode({0x05});

    ASTIA_CHECK(events.size() == 1);
    ASTIA_CHECK(events[0].code == key::f1 && events[0].pressed);

    events = decode({0x03});

    ASTIA_CHECK(events.size() == 1);
    ASTIA_CHECK(events[0].code == key::f5 && events[0].pressed);

    events = decode({0x83});

    ASTIA_CHECK(events.size() == 1);
    ASTIA_CHECK(events[0].code == key::f7 && events[0].pressed);

    events = decode({0xF0, 0x05});

    ASTIA_CHECK(events.size() == 1);
    ASTIA_CHECK(events[0].code == key::f1 && !events[0].pressed);
}

ASTIA_TEST(set2_letters_digits_and_text_keys) {
    auto events = decode({0x1C});

    ASTIA_CHECK(events.size() == 1 && events[0].code == key::a && events[0].pressed);

    events = decode({0x16});

    ASTIA_CHECK(events.size() == 1 && events[0].code == key::digit_1);

    events = decode({0x45});

    ASTIA_CHECK(events.size() == 1 && events[0].code == key::digit_0);

    events = decode({0x29});

    ASTIA_CHECK(events.size() == 1 && events[0].code == key::space);

    events = decode({0x66});

    ASTIA_CHECK(events.size() == 1 && events[0].code == key::backspace);

    events = decode({0x5A});

    ASTIA_CHECK(events.size() == 1 && events[0].code == key::enter);

    events = decode({0x0D});

    ASTIA_CHECK(events.size() == 1 && events[0].code == key::tab);

    events = decode({0x76});

    ASTIA_CHECK(events.size() == 1 && events[0].code == key::escape);

    events = decode({0x4D});

    ASTIA_CHECK(events.size() == 1 && events[0].code == key::p);

    events = decode({0xF0, 0x1C});

    ASTIA_CHECK(events.size() == 1 && events[0].code == key::a && !events[0].pressed);
}

ASTIA_TEST(set2_modifier_make_and_break) {
    auto events = decode({0x12});

    ASTIA_CHECK(events.size() == 1 && events[0].code == key::left_shift);

    events = decode({0xF0, 0x12});

    ASTIA_CHECK(events.size() == 1 && events[0].code == key::left_shift &&
                !events[0].pressed);

    events = decode({0x59});

    ASTIA_CHECK(events.size() == 1 && events[0].code == key::right_shift);

    events = decode({0x14});

    ASTIA_CHECK(events.size() == 1 && events[0].code == key::left_ctrl);

    events = decode({0x11});

    ASTIA_CHECK(events.size() == 1 && events[0].code == key::left_alt);

    events = decode({0x58});

    ASTIA_CHECK(events.size() == 1 && events[0].code == key::caps_lock);
}

ASTIA_TEST(set2_extended_navigation) {
    struct sample {
        astia::u8 byte;
        key       code;
    };

    const sample samples[] = {
        {0x6C, key::home},       {0x75, key::up},         {0x7D, key::page_up},
        {0x6B, key::left},       {0x74, key::right},      {0x69, key::end},
        {0x72, key::down},       {0x7A, key::page_down},  {0x70, key::insert},
        {0x71, key::delete_key}, {0x47, key::home},       {0x48, key::up},
        {0x49, key::page_up},    {0x4B, key::left},       {0x4D, key::right},
        {0x4F, key::end},        {0x50, key::down},       {0x51, key::page_down},
        {0x52, key::insert},     {0x53, key::delete_key},
    };

    for (const sample& item : samples) {
        auto events = decode({0xE0, item.byte});

        ASTIA_CHECK(events.size() == 1);
        ASTIA_CHECK(events[0].code == item.code && events[0].pressed);
    }

    auto events = decode({0xE0, 0xF0, 0x6C});

    ASTIA_CHECK(events.size() == 1);
    ASTIA_CHECK(events[0].code == key::home && !events[0].pressed);
}

ASTIA_TEST(set2_extended_keypad) {
    auto events = decode({0xE0, 0x1C});

    ASTIA_CHECK(events.size() == 1 && events[0].code == key::keypad_enter);

    events = decode({0xE0, 0x4A});

    ASTIA_CHECK(events.size() == 1 && events[0].code == key::keypad_divide);

    events = decode({0xE0, 0x35});

    ASTIA_CHECK(events.size() == 1 && events[0].code == key::keypad_divide);

    events = decode({0xE0, 0x5A});

    ASTIA_CHECK(events.size() == 1 && events[0].code == key::keypad_enter);
}

ASTIA_TEST(set2_pause_is_swallowed_without_modifiers) {
    auto events = decode({0xE1, 0x14, 0x77, 0xE1, 0xF0, 0x14, 0xF0, 0x77});

    ASTIA_CHECK(events.empty());

    events = decode({0xE1, 0x14, 0x77, 0xE1, 0xF0, 0x14, 0xF0, 0x77, 0x1C});

    ASTIA_CHECK(events.size() == 1);
    ASTIA_CHECK(events[0].code == key::a);
}

ASTIA_TEST(set2_printscreen_is_swallowed) {
    auto events = decode({0xE0, 0x12, 0xE0, 0x7C});

    ASTIA_CHECK(events.empty());

    events = decode({0xE0, 0xF0, 0x7C, 0xE0, 0xF0, 0x12});

    ASTIA_CHECK(events.empty());

    events = decode({0xE0, 0x12, 0xE0, 0x7C, 0xE0, 0xF0, 0x7C, 0xE0, 0xF0, 0x12, 0x1C});

    ASTIA_CHECK(events.size() == 1);
    ASTIA_CHECK(events[0].code == key::a);
}

ASTIA_TEST(set2_malformed_pause_does_not_stick_modifiers) {
    auto events = decode({0xE1, 0x14, 0x78});

    ASTIA_CHECK(events.empty());

    events = decode({0xE1, 0x14, 0x78, 0x1C});

    ASTIA_CHECK(events.size() == 1);
    ASTIA_CHECK(events[0].code == key::a);

    events = decode({0xE1, 0x14, 0x78, 0xF0, 0x14, 0x1C});

    ASTIA_CHECK(events.size() == 2);
    ASTIA_CHECK(events[0].code == key::left_ctrl && !events[0].pressed);
    ASTIA_CHECK(events[1].code == key::a);
}

ASTIA_TEST(set2_protocol_and_error_bytes_are_ignored) {
    auto events = decode({0xFA, 0xFE, 0x00, 0xFF, 0xAA, 0xEE, 0xFC, 0xFD});

    ASTIA_CHECK(events.empty());

    events = decode({0xFA, 0xFE, 0x00, 0xFF, 0xAA, 0x1C});

    ASTIA_CHECK(events.size() == 1);
    ASTIA_CHECK(events[0].code == key::a);
}

ASTIA_TEST(set2_unknown_breaks_are_ignored) {
    auto events = decode({0xF0, 0x00});

    ASTIA_CHECK(events.empty());

    events = decode({0xE0, 0x5B});

    ASTIA_CHECK(events.empty());

    events = decode({0xE0, 0xF0, 0x5B});

    ASTIA_CHECK(events.empty());
}

ASTIA_TEST(set2_fake_shifts_and_extended_prefix_recovery) {
    auto events = decode({0xE0, 0x12});

    ASTIA_CHECK(events.empty());

    events = decode({0xE0, 0x59});

    ASTIA_CHECK(events.empty());

    events = decode({0xE0, 0xF0, 0x12});

    ASTIA_CHECK(events.empty());

    events = decode({0xE0, 0xF0, 0x59, 0x1C});

    ASTIA_CHECK(events.size() == 1);
    ASTIA_CHECK(events[0].code == key::a && events[0].pressed);

    events = decode({0xE0, 0xE0, 0x75});

    ASTIA_CHECK(events.size() == 1);
    ASTIA_CHECK(events[0].code == key::up && events[0].pressed);

    events = decode({0xE0, 0xE1, 0x14, 0x77, 0xE1, 0xF0, 0x14, 0xF0, 0x77, 0xE0, 0x6B});

    ASTIA_CHECK(events.size() == 1);
    ASTIA_CHECK(events[0].code == key::left && events[0].pressed);
}

ASTIA_TEST(set2_pause_prime_stays_out_of_modifier_state) {
    auto events = decode({0xE1, 0x14, 0x77, 0xE1, 0xF0, 0x14});

    ASTIA_CHECK(events.empty());

    events = decode({0xE1, 0x14, 0x77, 0xE1, 0xF0, 0x14, 0xF0, 0x77, 0x12, 0x1C});

    ASTIA_CHECK(events.size() == 2);
    ASTIA_CHECK(events[0].code == key::left_shift && events[0].pressed);
    ASTIA_CHECK(events[1].code == key::a && events[1].pressed);
}

ASTIA_TEST(set2_error_recovery_never_leaves_break_state) {
    auto events = decode({0xF0});

    ASTIA_CHECK(events.empty());

    events = decode({0xF0, 0xF0});

    ASTIA_CHECK(events.empty());

    events = decode({0xF0, 0xF0, 0x1C});

    ASTIA_CHECK(events.size() == 1);
    ASTIA_CHECK(events[0].code == key::a && !events[0].pressed);

    events = decode({0xE0, 0xF0, 0xFA, 0x1C});

    ASTIA_CHECK(events.size() == 1);
    ASTIA_CHECK(events[0].code == key::a && events[0].pressed);
}
