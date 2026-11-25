#include "dl/dl.hpp"

#include "support/test.hpp"

namespace {

    using astia::mapper;
    using astia::dl::elf_segment_flag;
    using astia::dl::has;

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
