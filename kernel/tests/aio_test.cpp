#include "support/test.hpp"
#ifdef RENAME_NOREPLACE
#undef RENAME_NOREPLACE
#endif
#ifdef RENAME_EXCHANGE
#undef RENAME_EXCHANGE
#endif
#include <astia/api/aio.h>

ASTIA_TEST(aio_public_limits_cover_exact_ring_boundary) {
    ASTIA_CHECK(static_cast<unsigned>(IO_MAX_ENTRIES) >=
                static_cast<unsigned>(ASTIA_AIO_MAX_RING_ENTRIES));
    ASTIA_CHECK(ASTIA_AIO_MAX_RING_ENTRIES == 64);
    ASTIA_CHECK(ASTIA_AIO_MAX_RING_ENTRIES - 1 < ASTIA_AIO_MAX_RING_ENTRIES);
}

ASTIA_TEST(aio_cq_consume_acknowledges_notification_generation) {
    struct fixture {
        astia_rings   ring{};
        astia_cq_ring cq{};
    } value;

    value.ring.cq_off          = sizeof(astia_rings);
    value.cq.notify_generation = 7;

    astia_cq_consume(&value.ring, 1);

    ASTIA_CHECK(value.cq.head == 1);
    ASTIA_CHECK(value.cq.notify_ack == 7);

    value.cq.notify_generation = 8;

    astia_cq_consume(&value.ring, 0);
    ASTIA_CHECK(value.cq.notify_ack == 7);
}

ASTIA_TEST(aio_ring_lifecycle_initializes_and_reuses_cursors) {
    struct fixture {
        astia_rings   ring{};
        astia_sq_ring sq{};
        astia_cq_ring cq{};
    } value;

    value.ring.sq_off = sizeof(astia_rings);
    value.ring.cq_off = sizeof(astia_rings) + sizeof(astia_sq_ring);
    value.sq.entries  = 2;
    value.sq.mask     = 1;
    value.cq.entries  = 2;
    value.cq.mask     = 1;

    ASTIA_CHECK(astia_sq_space(&value.ring) == 2);
    __atomic_store_n(&value.sq.tail, 2u, __ATOMIC_RELEASE);
    ASTIA_CHECK(astia_sq_space(&value.ring) == 0);
    __atomic_store_n(&value.sq.head, 2u, __ATOMIC_RELEASE);
    ASTIA_CHECK(astia_sq_space(&value.ring) == 2);
}

ASTIA_TEST(aio_completion_and_cancel_publish_once) {
    struct fixture {
        astia_rings   ring{};
        astia_cq_ring cq{};
        astia_cqe     cqe[2]{};
    } value;

    value.ring.cq_off   = sizeof(astia_rings);
    value.ring.cqes_off = sizeof(astia_rings) + sizeof(astia_cq_ring);
    value.cq.entries    = 2;
    value.cq.mask       = 1;

    bool target_completed = false;

    auto post = [&](uint64_t tag, int32_t result) {
        if (tag == 0x70 && target_completed)
            return false;

        const uint32_t head = __atomic_load_n(&value.cq.head, __ATOMIC_ACQUIRE);
        const uint32_t tail = __atomic_load_n(&value.cq.tail, __ATOMIC_RELAXED);

        if (tail - head >= value.cq.entries)
            return false;

        value.cqe[tail & value.cq.mask] = {tag, result, 0};

        __atomic_store_n(&value.cq.tail, tail + 1, __ATOMIC_RELEASE);

        if (tag == 0x70)
            target_completed = true;

        return true;
    };

    ASTIA_CHECK(post(0x70, -ECANCELED));
    ASTIA_CHECK(post(0x71, 0));
    ASTIA_CHECK(astia_cq_ready(&value.ring) == 2);
    ASTIA_CHECK(astia_cq_peek(&value.ring)->user_data == 0x70);
    astia_cq_consume(&value.ring, 2);
    ASTIA_CHECK(!post(0x70, -ECANCELED));
    ASTIA_CHECK(astia_cq_ready(&value.ring) == 0);
}
