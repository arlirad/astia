#include "../src/io/aio/ring_layout.hpp"
#include "support/test.hpp"

#ifdef RENAME_NOREPLACE
#undef RENAME_NOREPLACE
#endif
#ifdef RENAME_EXCHANGE
#undef RENAME_EXCHANGE
#endif

#include <astia/api/aio.h>
#include <astia/atomic.hpp>

#include <cstring>

using namespace astia;
using astia::io::aio::ring::ring_layout;

namespace {
    struct ring_fixture {
        static constexpr u32 body_size = 2048;
        static constexpr u8  canary    = 0xa5;

        alignas(64) u8 before[64];
        alignas(64) u8 body[body_size];
        alignas(64) u8 after[64];

        ring_layout layout;

        explicit ring_fixture(u32 sq = 4, u32 cq = 64)
            : layout(ring_layout::make(sq, cq)) {
            std::memset(before, canary, sizeof before);
            std::memset(body, canary, sizeof body);
            std::memset(after, canary, sizeof after);
            std::memset(body, 0, used_bytes());

            astia_rings* r = header();

            r->magic      = RINGS_MAGIC;
            r->version    = API_VERSION;
            r->features   = FEAT_SINGLE_MMAP;
            r->sq_entries = layout.sq_entries;
            r->cq_entries = layout.cq_entries;
            r->sq_off     = layout.sq_off;
            r->cq_off     = layout.cq_off;
            r->sqes_off   = layout.sqes_off;
            r->cqes_off   = layout.cqes_off;

            astia_sq_ring* sq_ring = layout.sq(body);

            sq_ring->mask    = layout.sq_mask;
            sq_ring->entries = layout.sq_entries;

            astia_cq_ring* cq_ring = layout.cq(body);

            cq_ring->mask    = layout.cq_mask;
            cq_ring->entries = layout.cq_entries;
        }

        astia_rings* header() {
            return reinterpret_cast<astia_rings*>(body);
        }

        u32 used_bytes() const {
            return layout.cqes_off +
                   static_cast<u32>(sizeof(astia_cqe)) * layout.cq_entries;
        }

        bool canaries_intact() const {
            for (u32 i = 0; i < sizeof before; ++i) {
                if (before[i] != canary)
                    return false;
            }

            for (u32 i = 0; i < sizeof after; ++i) {
                if (after[i] != canary)
                    return false;
            }

            for (u32 i = used_bytes(); i < body_size; ++i) {
                if (body[i] != canary)
                    return false;
            }

            return true;
        }
    };

    template <typename Mutate>
    void expect_rejected_after(Mutate mutate) {
        ring_fixture fixture;

        ASTIA_CHECK(fixture.layout.header_matches(fixture.body));
        ASTIA_CHECK(fixture.canaries_intact());

        mutate(fixture);

        ASTIA_CHECK(!fixture.layout.header_matches(fixture.body));
        ASTIA_CHECK(!fixture.layout.post_cqe(fixture.body, 0xdead, -5));
        ASTIA_CHECK(fixture.canaries_intact());
    }
} // namespace

ASTIA_TEST(aio_ring_metadata_valid_geometry_posts_within_cached_bounds) {
    ring_fixture fixture;

    ASTIA_CHECK(fixture.layout.valid());
    ASTIA_CHECK(fixture.layout.header_matches(fixture.body));
    ASTIA_CHECK(fixture.layout.cq_cursors_ok(fixture.body));
    ASTIA_CHECK(fixture.layout.sq_cursors_ok(fixture.body));

    const u8* end = fixture.body + fixture.used_bytes();

    ASTIA_CHECK(reinterpret_cast<const u8*>(fixture.layout.cqe(fixture.body, 63)) +
                    sizeof(astia_cqe) <=
                end);

    ASTIA_CHECK(fixture.layout.post_cqe(fixture.body, 0x1234, -7));
    ASTIA_CHECK(fixture.layout.cq_pending(fixture.body) == 1);
    ASTIA_CHECK(fixture.layout.cqe(fixture.body, 0)->user_data == 0x1234);
    ASTIA_CHECK(fixture.layout.cqe(fixture.body, 0)->result == -7);
    ASTIA_CHECK(fixture.layout.cqe(fixture.body, 0)->flags == 0);

    for (u32 i = 1; i < fixture.layout.cq_entries; ++i) {
        ASTIA_CHECK(fixture.layout.post_cqe(fixture.body, i, 0));
    }

    ASTIA_CHECK(!fixture.layout.post_cqe(fixture.body, 0xffff, 0));
    ASTIA_CHECK(fixture.layout.cq_pending(fixture.body) == fixture.layout.cq_entries);
    ASTIA_CHECK(fixture.canaries_intact());
}

ASTIA_TEST(aio_ring_metadata_mutated_header_cannot_redirect_completion) {
    expect_rejected_after(
        [](ring_fixture& fixture) { fixture.header()->cq_off = 0x400; });

    expect_rejected_after(
        [](ring_fixture& fixture) { fixture.header()->cqes_off = 0x500; });

    expect_rejected_after(
        [](ring_fixture& fixture) { fixture.header()->sq_off = 0x400; });

    expect_rejected_after(
        [](ring_fixture& fixture) { fixture.header()->sqes_off = 0x600; });

    expect_rejected_after(
        [](ring_fixture& fixture) { fixture.header()->sq_entries = 1; });

    expect_rejected_after(
        [](ring_fixture& fixture) { fixture.header()->cq_entries = 1; });

    expect_rejected_after(
        [](ring_fixture& fixture) { fixture.layout.cq(fixture.body)->mask = 0; });

    expect_rejected_after(
        [](ring_fixture& fixture) { fixture.layout.cq(fixture.body)->entries = 1; });

    expect_rejected_after(
        [](ring_fixture& fixture) { fixture.layout.sq(fixture.body)->mask = 0; });

    expect_rejected_after(
        [](ring_fixture& fixture) { fixture.layout.sq(fixture.body)->entries = 1; });
}

ASTIA_TEST(aio_ring_metadata_rejects_user_cursors_outside_cached_bounds) {
    ring_fixture fixture;

    astia_cq_ring* cq = fixture.layout.cq(fixture.body);

    astia::atomic_ref(cq->head).store(1, astia::memory_order::release);
    ASTIA_CHECK(!fixture.layout.cq_cursors_ok(fixture.body));
    ASTIA_CHECK(!fixture.layout.post_cqe(fixture.body, 1, 0));

    astia::atomic_ref(cq->head).store(0, astia::memory_order::release);
    astia::atomic_ref(cq->tail).store(fixture.layout.cq_entries,
                                      astia::memory_order::release);

    ASTIA_CHECK(fixture.layout.cq_cursors_ok(fixture.body));
    ASTIA_CHECK(!fixture.layout.post_cqe(fixture.body, 2, 0));

    astia::atomic_ref(cq->tail).store(fixture.layout.cq_entries - 1,
                                      astia::memory_order::release);
    ASTIA_CHECK(fixture.layout.post_cqe(fixture.body, 3, 0));

    astia_sq_ring* sq = fixture.layout.sq(fixture.body);

    astia::atomic_ref(sq->tail).store(fixture.layout.sq_entries + 1,
                                      astia::memory_order::release);

    ASTIA_CHECK(!fixture.layout.sq_cursors_ok(fixture.body));
    ASTIA_CHECK(fixture.canaries_intact());
}
