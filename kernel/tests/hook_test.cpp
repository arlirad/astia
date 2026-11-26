#include <astia/core/hook.hpp>

#include "support/test.hpp"

namespace {
    struct probe : astia::core::hook::event<probe, int&> {};

    // Same payload, different tag: must get its own registry.
    struct other : astia::core::hook::event<other, int&> {};

    using astia::usize;
    namespace priority = astia::core::hook::priority;
    using astia::core::hook::outcome;

    int sequence[8]{};
    int sequence_count = 0;
    int shared_calls   = 0;

    void reset() {
        probe::clear();
        other::clear();

        for (int& value : sequence) {
            value = 0;
        }

        sequence_count = 0;
        shared_calls   = 0;
    }

    outcome record_first(int&) {
        sequence[sequence_count++] = 1;

        return outcome::pass;
    }

    outcome record_second(int&) {
        sequence[sequence_count++] = 2;

        return outcome::pass;
    }

    outcome record_third(int&) {
        sequence[sequence_count++] = 3;

        return outcome::pass;
    }

    outcome set_value(int& value) {
        value = 42;

        return outcome::pass;
    }

    outcome stop_here(int&) {
        sequence[sequence_count++] = 9;

        return outcome::stop;
    }

    outcome remove_second(int&) {
        probe::remove("second");

        return outcome::pass;
    }

    outcome add_late(int&) {
        probe::add("added", 10, &record_third);

        return outcome::pass;
    }

    outcome count_call(int&) {
        ++shared_calls;

        return outcome::pass;
    }

    const char* const capacity_names[probe::capacity] = {
        "h0", "h1", "h2",  "h3",  "h4",  "h5",  "h6",  "h7",
        "h8", "h9", "h10", "h11", "h12", "h13", "h14", "h15",
    };
} // namespace

ASTIA_TEST(hook_empty_event_does_nothing) {
    reset();

    int value = 7;

    probe::emit(value);
    ASTIA_CHECK(probe::size() == 0);
    ASTIA_CHECK(value == 7);
    ASTIA_CHECK(probe::emit_until(value) == outcome::pass);
}

ASTIA_TEST(hook_rejects_duplicate_and_null) {
    reset();
    ASTIA_CHECK(probe::add("first", 0, &record_first));
    ASTIA_CHECK(!probe::add("first", 0, &record_second));
    ASTIA_CHECK(!probe::add(nullptr, 0, &record_second));
    ASTIA_CHECK(!probe::add("null-handler", 0, nullptr));
    ASTIA_CHECK(probe::size() == 1);
}

ASTIA_TEST(hook_lower_priority_runs_first) {
    reset();
    ASTIA_CHECK(probe::add("late", priority::late, &record_third));
    ASTIA_CHECK(probe::add("early", priority::early, &record_first));
    ASTIA_CHECK(probe::add("normal", priority::normal, &record_second));

    int value = 0;

    probe::emit(value);

    ASTIA_CHECK(sequence_count == 3);
    ASTIA_CHECK(sequence[0] == 1);
    ASTIA_CHECK(sequence[1] == 2);
    ASTIA_CHECK(sequence[2] == 3);
}

ASTIA_TEST(hook_equal_priority_keeps_registration_order) {
    reset();
    ASTIA_CHECK(probe::add("a", 0, &record_first));
    ASTIA_CHECK(probe::add("b", 0, &record_second));
    ASTIA_CHECK(probe::add("c", 0, &record_third));

    int value = 0;

    probe::emit(value);

    ASTIA_CHECK(sequence_count == 3);
    ASTIA_CHECK(sequence[0] == 1);
    ASTIA_CHECK(sequence[1] == 2);
    ASTIA_CHECK(sequence[2] == 3);
}

ASTIA_TEST(hook_remove_and_clear) {
    reset();
    ASTIA_CHECK(probe::add("a", 0, &record_first));
    ASTIA_CHECK(probe::add("b", 0, &record_second));
    ASTIA_CHECK(probe::remove("a"));
    ASTIA_CHECK(!probe::remove("a"));

    int value = 0;

    probe::emit(value);
    ASTIA_CHECK(sequence_count == 1);
    ASTIA_CHECK(sequence[0] == 2);

    probe::clear();
    ASTIA_CHECK(probe::size() == 0);
}

ASTIA_TEST(hook_payload_reaches_every_handler) {
    reset();
    ASTIA_CHECK(probe::add("set", 0, &set_value));

    int value = 0;

    probe::emit(value);
    ASTIA_CHECK(value == 42);
}

ASTIA_TEST(hook_emit_until_stops) {
    reset();
    ASTIA_CHECK(probe::add("first", 0, &record_first));
    ASTIA_CHECK(probe::add("stop", 0, &stop_here));
    ASTIA_CHECK(probe::add("last", 0, &record_third));

    int value = 0;

    ASTIA_CHECK(probe::emit_until(value) == outcome::stop);
    ASTIA_CHECK(sequence_count == 2);
    ASTIA_CHECK(sequence[0] == 1);
    ASTIA_CHECK(sequence[1] == 9);
}

ASTIA_TEST(hook_emit_uses_entry_snapshot) {
    reset();
    ASTIA_CHECK(probe::add("first", 0, &remove_second));
    ASTIA_CHECK(probe::add("second", 0, &record_second));
    ASTIA_CHECK(probe::add("adder", 10, &add_late));

    int value = 0;

    probe::emit(value);

    // "second" was removed mid-emit but was registered when emit started.
    ASTIA_CHECK(sequence_count == 1);
    ASTIA_CHECK(sequence[0] == 2);

    // The hook added mid-emit runs on the next emit, and "second" is gone.
    sequence_count = 0;

    probe::emit(value);
    ASTIA_CHECK(sequence_count == 1);
    ASTIA_CHECK(sequence[0] == 3);
}

ASTIA_TEST(hook_events_do_not_share_storage) {
    reset();
    ASTIA_CHECK(probe::add("probe", 0, &count_call));
    ASTIA_CHECK(other::add("other", 0, &count_call));
    ASTIA_CHECK(probe::size() == 1);
    ASTIA_CHECK(other::size() == 1);

    int value = 0;

    probe::emit(value);
    ASTIA_CHECK(shared_calls == 1);
    other::emit(value);
    ASTIA_CHECK(shared_calls == 2);
}

ASTIA_TEST(hook_capacity_is_bounded) {
    reset();

    for (usize i = 0; i < probe::capacity; ++i) {
        ASTIA_CHECK(probe::add(capacity_names[i], 0, &record_first));
    }

    ASTIA_CHECK(probe::size() == probe::capacity);
    ASTIA_CHECK(!probe::add("overflow", 0, &record_first));
}
