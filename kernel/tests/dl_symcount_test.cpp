#include "../../dl/symcount.h"
#include "test.hpp"

namespace {
    constexpr uint64_t symbol_slot    = sizeof(Elf64_Sym);
    constexpr uint64_t symtab_address = 0x10000000;

    ld_symcount_input make_input(const uint32_t* hash, uint64_t hash_bytes,
                                 const uint32_t* gnu, uint64_t gnu_bytes,
                                 uint64_t symtab_bytes, uint64_t strtab_offset) {
        ld_symcount_input input = {};

        input.symtab       = symtab_address;
        input.strtab       = symtab_address + strtab_offset;
        input.symtab_bytes = symtab_bytes;

        if (hash != 0) {
            input.hash       = hash;
            input.hash_bytes = hash_bytes;
        }

        if (gnu != 0) {
            input.gnu_hash  = gnu;
            input.gnu_bytes = gnu_bytes;
        }

        return input;
    }
} // namespace

ASTIA_TEST(dl_symbol_count_reads_sysv_nchain) {
    const uint32_t hash[] = {1, 5};
    const auto     input =
        make_input(hash, sizeof hash, 0, 0, 5 * symbol_slot, 5 * symbol_slot);

    uint32_t count = 0;

    ASTIA_CHECK(ld_symbol_count(&input, &count) == 0);
    ASTIA_CHECK(count == 5);
}

ASTIA_TEST(dl_symbol_count_rejects_sysv_count_beyond_symtab) {
    const uint32_t hash[] = {1, 5};
    const auto     input =
        make_input(hash, sizeof hash, 0, 0, 4 * symbol_slot, 4 * symbol_slot);

    uint32_t count = 0;

    ASTIA_CHECK(ld_symbol_count(&input, &count) == -1);
}

ASTIA_TEST(dl_symbol_count_rejects_truncated_sysv_header) {
    const uint32_t hash[] = {1, 5};
    const auto     input  = make_input(hash, 4, 0, 0, 5 * symbol_slot, 5 * symbol_slot);

    uint32_t count = 0;

    ASTIA_CHECK(ld_symbol_count(&input, &count) == -1);
}

ASTIA_TEST(dl_symbol_count_reads_gnu_undefined_only_table) {
    const uint32_t hash[] = {1, 3, 1, 0, 0, 0, 0};
    const auto     input =
        make_input(0, 0, hash, sizeof hash, 3 * symbol_slot, 3 * symbol_slot);

    uint32_t count = 0;

    ASTIA_CHECK(ld_symbol_count(&input, &count) == 0);
    ASTIA_CHECK(count == 3);
}

ASTIA_TEST(dl_symbol_count_walks_gnu_chain) {
    const uint32_t hash[] = {2, 1, 1, 0, 0, 0, 1, 2, 0, 1};
    const auto     input =
        make_input(0, 0, hash, sizeof hash, 3 * symbol_slot, 3 * symbol_slot);

    uint32_t count = 0;

    ASTIA_CHECK(ld_symbol_count(&input, &count) == 0);
    ASTIA_CHECK(count == 3);
}

ASTIA_TEST(dl_symbol_count_accepts_exact_adjacency) {
    const uint32_t hash[] = {1, 1, 1, 0, 0, 0, 0};
    const auto     input =
        make_input(0, 0, hash, sizeof hash, 4 * symbol_slot, 4 * symbol_slot);

    uint32_t count = 0;

    ASTIA_CHECK(ld_symbol_count(&input, &count) == 0);
    ASTIA_CHECK(count == 4);
}

ASTIA_TEST(dl_symbol_count_rejects_adjacency_beyond_symtab_extent) {
    const uint32_t hash[] = {1, 1, 1, 0, 0, 0, 0};
    const auto     input =
        make_input(0, 0, hash, sizeof hash, 2 * symbol_slot, 4 * symbol_slot);

    uint32_t count = 0;

    ASTIA_CHECK(ld_symbol_count(&input, &count) == -1);
}

ASTIA_TEST(dl_symbol_count_clamps_unaligned_adjacency) {
    const uint32_t hash[] = {1, 1, 1, 0, 0, 0, 0};
    const auto     input =
        make_input(0, 0, hash, sizeof hash, 8 * symbol_slot, 4 * symbol_slot + 8);

    uint32_t count = 0;

    ASTIA_CHECK(ld_symbol_count(&input, &count) == 0);
    ASTIA_CHECK(count == 1);
}

ASTIA_TEST(dl_symbol_count_rejects_strtab_inside_symtab) {
    const uint32_t hash[] = {1, 3, 1, 0, 0, 0, 0};
    const auto input = make_input(0, 0, hash, sizeof hash, 3 * symbol_slot, symbol_slot);

    uint32_t count = 0;

    ASTIA_CHECK(ld_symbol_count(&input, &count) == -1);
}

ASTIA_TEST(dl_symbol_count_rejects_unterminated_gnu_chain) {
    const uint32_t hash[] = {1, 1, 1, 0, 0, 0, 1, 0};
    const auto     input =
        make_input(0, 0, hash, sizeof hash, 8 * symbol_slot, 8 * symbol_slot);

    uint32_t count = 0;

    ASTIA_CHECK(ld_symbol_count(&input, &count) == -1);
}

ASTIA_TEST(dl_symbol_count_rejects_gnu_bucket_beyond_chain) {
    const uint32_t hash[] = {1, 1, 1, 0, 0, 0, 5};
    const auto     input =
        make_input(0, 0, hash, sizeof hash, 6 * symbol_slot, 6 * symbol_slot);

    uint32_t count = 0;

    ASTIA_CHECK(ld_symbol_count(&input, &count) == -1);
}

ASTIA_TEST(dl_symbol_count_rejects_gnu_header_beyond_segment) {
    const uint32_t hash[] = {4, 1, 2, 0, 0, 0};
    const auto     input =
        make_input(0, 0, hash, sizeof hash, 6 * symbol_slot, 6 * symbol_slot);

    uint32_t count = 0;

    ASTIA_CHECK(ld_symbol_count(&input, &count) == -1);
}

ASTIA_TEST(dl_symbol_count_rejects_gnu_count_beyond_symtab) {
    const uint32_t hash[] = {1, 3, 1, 0, 0, 0, 0};
    const auto     input =
        make_input(0, 0, hash, sizeof hash, 2 * symbol_slot, 2 * symbol_slot);

    uint32_t count = 0;

    ASTIA_CHECK(ld_symbol_count(&input, &count) == -1);
}

ASTIA_TEST(dl_symbol_count_rejects_bloom_overflow) {
    const uint32_t hash[] = {1, 1, 0x80000000u, 0};
    const auto     input =
        make_input(0, 0, hash, sizeof hash, 2 * symbol_slot, 2 * symbol_slot);

    uint32_t count = 0;

    ASTIA_CHECK(ld_symbol_count(&input, &count) == -1);
}

ASTIA_TEST(dl_symbol_count_rejects_missing_tables) {
    const auto input = make_input(0, 0, 0, 0, 1 * symbol_slot, symbol_slot);

    uint32_t count = 0;

    ASTIA_CHECK(ld_symbol_count(&input, &count) == -1);
}
