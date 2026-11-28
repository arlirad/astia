#include "run_queue.hpp"
#include "support/test.hpp"

namespace {
    using namespace astia;
    using queue = proc::run_queue<struct item, 32>;

    struct item {
        u64  vruntime = 0;
        u8   priority = 0;
        bool realtime = false;
        bool eligible = true;
    };

    queue::policy policy_for(const item& value) {
        return {.vruntime = value.vruntime,
                .priority = value.priority,
                .realtime = value.realtime};
    }

    struct eligible {
        bool operator()(const item* value) const {
            return value->eligible;
        }
    };

    u32 insert(queue& values, item& value, u32& count) {
        const u32 slot = values.insert(&value, policy_for(value));

        if (slot != queue::none)
            ++count;

        return slot;
    }

    void fill_normal(queue& values, item* items, u32 count, const u64 (&vruntimes)[32]) {
        u32 inserted = 0;

        for (u32 i = 0; i < count; ++i) {
            items[i].vruntime = vruntimes[i];

            insert(values, items[i], inserted);
        }

        ASTIA_CHECK(inserted == count);
    }
} // namespace

ASTIA_TEST(run_queue_normal_orders_by_vruntime_then_sequence) {
    queue values;
    item  items[4];

    const u64 vruntimes[32]{30, 10, 20, 10};

    fill_normal(values, items, 4, vruntimes);

    const eligible ready{};

    ASTIA_CHECK(values.pick(ready) == &items[1]);
    ASTIA_CHECK(values.pick(ready) == &items[3]);
    ASTIA_CHECK(values.pick(ready) == &items[2]);
    ASTIA_CHECK(values.pick(ready) == &items[0]);
    ASTIA_CHECK(values.pick(ready) == nullptr);
    ASTIA_CHECK(values.empty());
}

ASTIA_TEST(run_queue_zero_initialized_realtime_starts_empty) {
    queue values{};
    item  first;
    item  second;

    first.realtime  = true;
    first.priority  = 3;
    second.realtime = true;
    second.priority = 3;

    u32 inserted = 0;

    insert(values, first, inserted);
    insert(values, second, inserted);

    const eligible ready{};

    ASTIA_CHECK(values.pick(ready) == &first);
    ASTIA_CHECK(values.pick(ready) == &second);
}

ASTIA_TEST(run_queue_realtime_reactivation_keeps_sequence_order) {
    queue values;
    item  first;
    item  second;
    item  third;

    first.realtime  = true;
    first.priority  = 4;
    first.eligible  = false;
    second.realtime = true;
    second.priority = 4;
    third.realtime  = true;
    third.priority  = 4;

    u32       inserted   = 0;
    const u32 first_slot = insert(values, first, inserted);

    insert(values, second, inserted);
    insert(values, third, inserted);

    const eligible ready{};

    ASTIA_CHECK(values.pick(ready) == &second);
    ASTIA_CHECK(!values.is_active(first_slot));

    first.eligible = true;

    ASTIA_CHECK(values.reactivate(first_slot, policy_for(first)));
    ASTIA_CHECK(values.pick(ready) == &first);
    ASTIA_CHECK(values.pick(ready) == &third);
}

ASTIA_TEST(run_queue_realtime_precedes_normal_and_is_fifo) {
    queue values;
    item  items[5];

    u32 inserted = 0;

    items[0].realtime = true;
    items[0].priority = 1;
    items[1].realtime = true;
    items[1].priority = 5;
    items[2].realtime = true;
    items[2].priority = 5;
    items[3].realtime = true;
    items[3].priority = 3;
    items[4].vruntime = 0;

    for (auto& value : items) {
        insert(values, value, inserted);
    }

    ASTIA_CHECK(inserted == 5);

    const eligible ready{};

    ASTIA_CHECK(values.pick(ready) == &items[1]);
    ASTIA_CHECK(values.pick(ready) == &items[2]);
    ASTIA_CHECK(values.pick(ready) == &items[3]);
    ASTIA_CHECK(values.pick(ready) == &items[0]);
    ASTIA_CHECK(values.pick(ready) == &items[4]);
    ASTIA_CHECK(values.pick(ready) == nullptr);
}

ASTIA_TEST(run_queue_defers_ineligible_entries_until_reactivated) {
    queue values;
    item  realtime;
    item  normal;
    item  later;

    realtime.realtime = true;
    realtime.priority = 9;
    realtime.eligible = false;
    normal.vruntime   = 20;
    later.vruntime    = 30;

    u32       inserted      = 0;
    const u32 realtime_slot = insert(values, realtime, inserted);

    insert(values, normal, inserted);
    insert(values, later, inserted);

    const eligible ready{};

    const u64 before = values.probes();

    ASTIA_CHECK(values.pick(ready) == &normal);
    ASTIA_CHECK(values.probes() - before == 2);
    ASTIA_CHECK(values.size() == 2);
    ASTIA_CHECK(!values.is_active(realtime_slot));

    realtime.eligible = true;

    ASTIA_CHECK(values.reactivate(realtime_slot, policy_for(realtime)));
    ASTIA_CHECK(values.pick(ready) == &realtime);
    ASTIA_CHECK(values.pick(ready) == &later);
    ASTIA_CHECK(values.pick(ready) == nullptr);

    (void) inserted;
}

ASTIA_TEST(run_queue_erases_arbitrary_entries) {
    queue values;
    item  items[8];

    const u64 vruntimes[32]{5, 1, 7, 3, 9, 0, 6, 2};

    u32 inserted = 0;
    u32 slots[8]{};

    for (u32 i = 0; i < 8; ++i) {
        items[i].vruntime = vruntimes[i];
        slots[i]          = insert(values, items[i], inserted);
    }

    values.erase(slots[1]);
    values.erase(slots[2]);
    values.erase(slots[5]);

    ASTIA_CHECK(values.size() == 5);

    const eligible ready{};

    ASTIA_CHECK(values.pick(ready) == &items[7]);
    ASTIA_CHECK(values.pick(ready) == &items[3]);
    ASTIA_CHECK(values.pick(ready) == &items[0]);
    ASTIA_CHECK(values.pick(ready) == &items[6]);
    ASTIA_CHECK(values.pick(ready) == &items[4]);
    ASTIA_CHECK(values.pick(ready) == nullptr);
}

ASTIA_TEST(run_queue_capacity_is_bounded_and_slots_are_reused) {
    queue values;
    item  items[33];

    u32 slots[32]{};

    for (u32 i = 0; i < 32; ++i) {
        items[i].vruntime = 32 - i;
        slots[i]          = values.insert(&items[i], policy_for(items[i]));

        ASTIA_CHECK(slots[i] != queue::none);
    }

    ASTIA_CHECK(values.size() == 32);
    ASTIA_CHECK(values.insert(&items[32], policy_for(items[32])) == queue::none);

    values.erase(slots[0]);

    ASTIA_CHECK(values.size() == 31);
    ASTIA_CHECK(values.insert(&items[32], policy_for(items[32])) != queue::none);
    ASTIA_CHECK(values.size() == 32);
}

ASTIA_TEST(run_queue_normal_ties_preserve_enqueue_order) {
    queue values;
    item  items[32];

    for (u32 i = 0; i < 32; ++i) {
        items[i].vruntime = 0;

        ASTIA_CHECK(values.insert(&items[i], policy_for(items[i])) != queue::none);
    }

    const eligible ready{};

    for (u32 i = 0; i < 32; ++i) {
        ASTIA_CHECK(values.pick(ready) == &items[i]);
    }

    ASTIA_CHECK(values.pick(ready) == nullptr);
}

ASTIA_TEST(run_queue_pick_examines_only_the_selected_entry) {
    queue values;
    item  items[32];

    for (u32 i = 0; i < 32; ++i) {
        items[i].vruntime = 32 - i;

        ASTIA_CHECK(values.insert(&items[i], policy_for(items[i])) != queue::none);
    }

    const eligible ready{};

    const u64 before = values.probes();

    for (u32 i = 0; i < 32; ++i) {
        ASTIA_CHECK(values.pick(ready) == &items[31 - i]);
    }

    ASTIA_CHECK(values.probes() - before == 32);
    ASTIA_CHECK(values.empty());
}

ASTIA_TEST(run_queue_realtime_pick_examines_only_the_selected_entry) {
    queue values;
    item  items[32];

    for (u32 i = 0; i < 32; ++i) {
        items[i].realtime = true;
        items[i].priority = static_cast<u8>(i * 8);

        ASTIA_CHECK(values.insert(&items[i], policy_for(items[i])) != queue::none);
    }

    const eligible ready{};

    const u64 before = values.probes();

    for (u32 i = 0; i < 32; ++i) {
        ASTIA_CHECK(values.pick(ready) == &items[31 - i]);
    }

    ASTIA_CHECK(values.probes() - before == 32);
    ASTIA_CHECK(values.empty());
}

ASTIA_TEST(run_queue_full_admission_hands_off_and_queues_caller_once) {
    queue values;
    item  items[33];

    for (u32 i = 0; i < 32; ++i) {
        items[i].vruntime = i;

        ASTIA_CHECK(values.insert(&items[i], policy_for(items[i])) != queue::none);
    }

    items[32].vruntime = 0;

    const eligible ready{};

    const auto admitted =
        values.insert_or_handoff(&items[32], policy_for(items[32]), ready);

    ASTIA_CHECK(admitted.queued);
    ASTIA_CHECK(admitted.next == &items[0]);
    ASTIA_CHECK(admitted.next != &items[32]);
    ASTIA_CHECK(values.size() == 32);

    u32 occurrences[33]{};

    values.for_each([&](u32, item* value, u64, bool) {
        for (u32 i = 0; i < 33; ++i) {
            if (value == &items[i])
                ++occurrences[i];
        }
    });

    ASTIA_CHECK(occurrences[0] == 0);
    ASTIA_CHECK(occurrences[32] == 1);

    for (u32 i = 1; i < 32; ++i) {
        ASTIA_CHECK(occurrences[i] == 1);
    }

    ASTIA_CHECK(values.pick(ready) == &items[32]);

    for (u32 i = 1; i < 32; ++i) {
        ASTIA_CHECK(values.pick(ready) == &items[i]);
    }

    ASTIA_CHECK(values.pick(ready) == nullptr);
}

ASTIA_TEST(run_queue_full_admission_reports_when_nothing_is_eligible) {
    queue values;
    item  items[33];

    for (u32 i = 0; i < 32; ++i) {
        items[i].eligible = false;
        items[i].vruntime = i;

        ASTIA_CHECK(values.insert(&items[i], policy_for(items[i])) != queue::none);
    }

    const eligible ready{};

    const auto admitted =
        values.insert_or_handoff(&items[32], policy_for(items[32]), ready);

    ASTIA_CHECK(!admitted.queued);
    ASTIA_CHECK(admitted.next == nullptr);
    ASTIA_CHECK(admitted.slot == queue::none);
    ASTIA_CHECK(values.size() == 32);
}

ASTIA_TEST(run_queue_admission_inserts_without_handoff_when_a_slot_is_free) {
    queue values;
    item  first;
    item  second;

    first.vruntime  = 1;
    second.vruntime = 0;

    const eligible ready{};

    const auto first_admitted =
        values.insert_or_handoff(&first, policy_for(first), ready);

    ASTIA_CHECK(first_admitted.queued);
    ASTIA_CHECK(first_admitted.next == nullptr);
    ASTIA_CHECK(first_admitted.slot != queue::none);

    const auto second_admitted =
        values.insert_or_handoff(&second, policy_for(second), ready);

    ASTIA_CHECK(second_admitted.queued);
    ASTIA_CHECK(second_admitted.next == nullptr);
    ASTIA_CHECK(values.size() == 2);
    ASTIA_CHECK(values.pick(ready) == &second);
    ASTIA_CHECK(values.pick(ready) == &first);
}
