#include "dl/dl.hpp"
#include "dl/dl_registry.hpp"
#include "support/test.hpp"

namespace {
    using astia::mapper;
    using astia::dl::elf_segment_flag;
    using astia::dl::has;

    struct registry_owner {
        astia::u32 id = 0;
    };

    struct registry_binding {
        astia::u32 value = 0;
    };

    using registry_table =
        astia::dl::detail::row_table<registry_owner, registry_binding, 2, 1>;
} // namespace

ASTIA_TEST(dl_elf_flags_keep_the_elf_bit_order) {
    // PF_X = 1, PF_W = 2, PF_R = 4.  The ABI's PROT_* order is the reverse,
    // so these numbers are the guard against a silent mix-up.
    ASTIA_CHECK(static_cast<astia::u32>(elf_segment_flag::execute) == 1);
    ASTIA_CHECK(static_cast<astia::u32>(elf_segment_flag::write) == 2);
    ASTIA_CHECK(static_cast<astia::u32>(elf_segment_flag::read) == 4);
}

ASTIA_TEST(dl_elf_flags_round_trip_through_mapper) {
    for (astia::i64 raw = 0; raw <= 7; ++raw) {
        auto flags = mapper<elf_segment_flag>::from(raw);

        ASTIA_CHECK(flags);
        ASTIA_CHECK(static_cast<astia::i64>(flags.value) == raw);
    }

    ASTIA_CHECK(!mapper<elf_segment_flag>::from(-1));
    ASTIA_CHECK(!mapper<elf_segment_flag>::from(8));
}

ASTIA_TEST(dl_elf_flags_decompose) {
    auto flags = mapper<elf_segment_flag>::from(7);

    ASTIA_CHECK(flags);
    ASTIA_CHECK(has(flags.value, elf_segment_flag::read));
    ASTIA_CHECK(has(flags.value, elf_segment_flag::write));
    ASTIA_CHECK(has(flags.value, elf_segment_flag::execute));

    flags = mapper<elf_segment_flag>::from(5); // PF_R | PF_X: text

    ASTIA_CHECK(flags);
    ASTIA_CHECK(has(flags.value, elf_segment_flag::read));
    ASTIA_CHECK(!has(flags.value, elf_segment_flag::write));
    ASTIA_CHECK(has(flags.value, elf_segment_flag::execute));
}

ASTIA_TEST(dl_adopt_reuses_the_staging_row_when_the_table_is_full) {
    registry_table rows;
    registry_owner staging{1}, target{2}, other{3};

    auto* staged = rows.make(&staging);

    ASTIA_CHECK(staged != nullptr);

    staged->items[staged->count++] = registry_binding{42};

    auto* occupied = rows.make(&other);

    ASTIA_CHECK(occupied != nullptr);

    occupied->items[occupied->count++] = registry_binding{7};

    astia::u32 released = 0;

    const bool adopted =
        rows.adopt(&target, &staging, [&released](auto&) { ++released; });

    ASTIA_CHECK(adopted);
    ASTIA_CHECK(released == 0);
    ASTIA_CHECK(rows.find(&staging) == nullptr);

    auto* adopted_row = rows.find(&target);

    ASTIA_CHECK(adopted_row == staged);
    ASTIA_CHECK(adopted_row->count == 1);
    ASTIA_CHECK(adopted_row->items[0].value == 42);
}

ASTIA_TEST(dl_adopt_moves_the_staging_row_into_an_empty_target_row) {
    registry_table rows;
    registry_owner staging{1}, target{2};

    auto* staged = rows.make(&staging);

    ASTIA_CHECK(staged != nullptr);

    staged->items[staged->count++] = registry_binding{42};

    auto* destination = rows.make(&target);

    ASTIA_CHECK(destination != nullptr);
    ASTIA_CHECK(destination->count == 0);

    astia::u32 released = 0;

    const bool adopted =
        rows.adopt(&target, &staging, [&released](auto&) { ++released; });

    ASTIA_CHECK(adopted);
    ASTIA_CHECK(released == 0);
    ASTIA_CHECK(rows.find(&staging) == nullptr);

    auto* adopted_row = rows.find(&target);

    ASTIA_CHECK(adopted_row == destination);
    ASTIA_CHECK(adopted_row->count == 1);
    ASTIA_CHECK(adopted_row->items[0].value == 42);
}

ASTIA_TEST(dl_adopt_releases_the_staging_row_when_the_target_is_occupied) {
    registry_table rows;
    registry_owner staging{1}, target{2};

    auto* staged = rows.make(&staging);

    ASTIA_CHECK(staged != nullptr);

    staged->items[staged->count++] = registry_binding{42};

    auto* destination = rows.make(&target);

    ASTIA_CHECK(destination != nullptr);

    destination->items[destination->count++] = registry_binding{7};

    astia::u32 released = 0;

    const bool adopted =
        rows.adopt(&target, &staging, [&released](auto&) { ++released; });

    ASTIA_CHECK(!adopted);
    ASTIA_CHECK(released == 1);
    ASTIA_CHECK(rows.find(&staging) == nullptr);

    auto* retained = rows.find(&target);

    ASTIA_CHECK(retained == destination);
    ASTIA_CHECK(retained->count == 1);
    ASTIA_CHECK(retained->items[0].value == 7);
}
