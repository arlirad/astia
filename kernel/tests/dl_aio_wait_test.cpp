#include "../../dl/aio_wait.h"
#include "test.hpp"

#include <cstddef>

namespace {
    constexpr uint32_t sq_entries = 4;
    constexpr uint32_t cq_entries = 8;

    /* Laid out the way the kernel maps a ring, so the test drives the real
       astia_sqe_slot/astia_cq_peek/astia_cq_consume arithmetic. */
    struct fake_ring {
        astia_rings   header;
        astia_sq_ring sq;
        astia_cq_ring cq;
        astia_sqe     sqes[sq_entries];
        astia_cqe     cqes[cq_entries];

        astia_rings* init();
    };

    struct fake_backend {
        fake_ring ring;
        long      submit_error = 0;
        bool      post_cancel  = false;
        uint64_t  clock        = 1000;
        uint64_t  clock_step   = 0;
        uint32_t  submit_calls = 0;
        uint32_t  yields       = 0;
        uint32_t  last_count   = 0;
        bool      consumed_sqe = false;
        astia_sqe last_sqe     = {};
    };

    fake_backend* g_active;

    void push_cqe(astia_rings* ring, uint64_t tag, int32_t result) {
        astia_cq_ring* cq    = astia_rings_cq(ring);
        astia_cqe*     entry = &astia_rings_cqes(ring)[cq->tail & cq->mask];

        entry->user_data = tag;
        entry->result    = result;
        entry->flags     = 0;
        cq->tail += 1;
    }

    long fake_submit(astia_rings* ring, uint32_t count) {
        fake_backend* backend = g_active;

        backend->submit_calls += 1;
        backend->last_count   = count;
        backend->consumed_sqe = false;

        if (backend->submit_error < 0)
            return backend->submit_error;

        astia_sq_ring* sq = astia_rings_sq(ring);

        while (sq->head != sq->tail) {
            backend->last_sqe     = astia_rings_sqes(ring)[sq->head & sq->mask];
            backend->consumed_sqe = true;
            sq->head += 1;
        }

        /* A cancelling kernel posts the target's -ECANCELED and then the
           cancellation's own acknowledgement. */
        if (backend->post_cancel && backend->consumed_sqe &&
            backend->last_sqe.op == OP_CANCEL) {
            push_cqe(ring, backend->last_sqe.offset, -ECANCELED);
            push_cqe(ring, backend->last_sqe.user_data, 0);

            backend->post_cancel = false;
        }

        return 0;
    }

    long fake_yield(void) {
        fake_backend* backend = g_active;

        backend->yields += 1;
        backend->clock += backend->clock_step;

        return 0;
    }

    uint64_t fake_now(void) {
        return g_active->clock;
    }

    ld_aio_backend make_backend() {
        return {.sqe_slot   = astia_sqe_slot,
                .sq_publish = astia_sq_publish,
                .submit     = fake_submit,
                .yield      = fake_yield,
                .cq_peek    = astia_cq_peek,
                .cq_consume = astia_cq_consume,
                .now_ns     = fake_now};
    }

    astia_rings* fake_ring::init() {
        header = {};

        sq = {};
        cq = {};

        header.magic      = RINGS_MAGIC;
        header.version    = API_VERSION;
        header.sq_entries = sq_entries;
        header.cq_entries = cq_entries;
        header.sq_off     = static_cast<uint32_t>(offsetof(fake_ring, sq));
        header.cq_off     = static_cast<uint32_t>(offsetof(fake_ring, cq));
        header.sqes_off   = static_cast<uint32_t>(offsetof(fake_ring, sqes));
        header.cqes_off   = static_cast<uint32_t>(offsetof(fake_ring, cqes));

        sq.mask    = sq_entries - 1;
        sq.entries = sq_entries;
        cq.mask    = cq_entries - 1;
        cq.entries = cq_entries;

        return &header;
    }
} // namespace

ASTIA_TEST(dl_aio_submit_publishes_the_operation) {
    fake_backend   fake;
    ld_aio_backend backend = make_backend();
    astia_rings*   ring    = fake.ring.init();

    g_active = &fake;

    astia_sqe sqe = {};

    sqe.op        = OP_READ;
    sqe.fd        = 7;
    sqe.user_data = 11;

    ASTIA_CHECK(ld_aio_submit(&backend, ring, &sqe) == 0);
    ASTIA_CHECK(fake.last_count == 1);
    ASTIA_CHECK(fake.last_sqe.op == OP_READ);
    ASTIA_CHECK(fake.last_sqe.fd == 7);
    ASTIA_CHECK(fake.last_sqe.user_data == 11);
    ASTIA_CHECK(astia_rings_sq(ring)->head == astia_rings_sq(ring)->tail);
}

ASTIA_TEST(dl_aio_submit_propagates_a_kick_failure) {
    fake_backend   fake;
    ld_aio_backend backend = make_backend();
    astia_rings*   ring    = fake.ring.init();

    g_active          = &fake;
    fake.submit_error = -EIO;

    astia_sqe sqe = {};

    sqe.user_data = 3;

    ASTIA_CHECK(ld_aio_submit(&backend, ring, &sqe) == -EIO);
}

ASTIA_TEST(dl_aio_wait_returns_the_matching_completion_without_polling) {
    fake_backend   fake;
    ld_aio_backend backend = make_backend();
    astia_rings*   ring    = fake.ring.init();

    g_active = &fake;

    push_cqe(ring, 5, 42);

    int timed_out = 1;

    ASTIA_CHECK(ld_aio_wait(&backend, ring, 5, &ld_aio_wait_bounds, &timed_out) == 42);
    ASTIA_CHECK(timed_out == 0);
    ASTIA_CHECK(fake.yields == 0);
    ASTIA_CHECK(fake.submit_calls == 0);
    ASTIA_CHECK(astia_cq_ready(ring) == 0);
}

ASTIA_TEST(dl_aio_wait_drops_older_completions) {
    fake_backend   fake;
    ld_aio_backend backend = make_backend();
    astia_rings*   ring    = fake.ring.init();

    g_active = &fake;

    push_cqe(ring, 4, -EIO);
    push_cqe(ring, 5, 42);

    int timed_out = 0;

    ASTIA_CHECK(ld_aio_wait(&backend, ring, 5, &ld_aio_wait_bounds, &timed_out) == 42);
    ASTIA_CHECK(timed_out == 0);
    ASTIA_CHECK(astia_cq_ready(ring) == 0);
}

ASTIA_TEST(dl_aio_wait_times_out_when_no_completion_arrives) {
    fake_backend   fake;
    ld_aio_backend backend = make_backend();
    astia_rings*   ring    = fake.ring.init();

    g_active = &fake;

    const ld_aio_bounds bounds = {250, 1000000};

    fake.clock_step = 100;

    int timed_out = 0;

    ASTIA_CHECK(ld_aio_wait(&backend, ring, 5, &bounds, &timed_out) == -ETIMEDOUT);
    ASTIA_CHECK(timed_out == 1);
    ASTIA_CHECK(fake.yields == 3);
}

ASTIA_TEST(dl_aio_wait_bounds_the_poll_count_without_a_clock) {
    fake_backend   fake;
    ld_aio_backend backend = make_backend();
    astia_rings*   ring    = fake.ring.init();

    g_active = &fake;

    const ld_aio_bounds bounds = {0, 5};

    int timed_out = 0;

    ASTIA_CHECK(ld_aio_wait(&backend, ring, 5, &bounds, &timed_out) == -ETIMEDOUT);
    ASTIA_CHECK(timed_out == 1);
    ASTIA_CHECK(fake.yields == 5);
}

ASTIA_TEST(dl_aio_wait_propagates_a_failed_kick) {
    fake_backend   fake;
    ld_aio_backend backend = make_backend();
    astia_rings*   ring    = fake.ring.init();

    g_active          = &fake;
    fake.submit_error = -EIO;

    const ld_aio_bounds bounds = {0, 100};

    int timed_out = 1;

    ASTIA_CHECK(ld_aio_wait(&backend, ring, 5, &bounds, &timed_out) == -EIO);
    ASTIA_CHECK(timed_out == 0);
    ASTIA_CHECK(fake.yields == 0);
}

ASTIA_TEST(dl_aio_cancel_submits_and_drains_the_cancellation) {
    fake_backend   fake;
    ld_aio_backend backend = make_backend();
    astia_rings*   ring    = fake.ring.init();

    g_active = &fake;

    fake.post_cancel = true;

    ASTIA_CHECK(ld_aio_cancel(&backend, ring, 77, 78, &ld_aio_cancel_bounds) == 0);
    ASTIA_CHECK(fake.last_sqe.op == OP_CANCEL);
    ASTIA_CHECK(fake.last_sqe.offset == 77);
    ASTIA_CHECK(fake.last_sqe.user_data == 78);
    ASTIA_CHECK(astia_cq_ready(ring) == 0);
    ASTIA_CHECK(fake.yields == 0);
}

ASTIA_TEST(dl_aio_cancel_returns_when_the_drain_grace_expires) {
    fake_backend   fake;
    ld_aio_backend backend = make_backend();
    astia_rings*   ring    = fake.ring.init();

    g_active = &fake;

    const ld_aio_bounds bounds = {0, 4};

    ASTIA_CHECK(ld_aio_cancel(&backend, ring, 77, 78, &bounds) == 0);
    ASTIA_CHECK(fake.yields == 4);
}

ASTIA_TEST(dl_aio_timeout_then_cancel_leaves_the_ring_clean) {
    fake_backend   fake;
    ld_aio_backend backend = make_backend();
    astia_rings*   ring    = fake.ring.init();

    g_active = &fake;

    astia_sqe sqe = {};

    sqe.op        = OP_OPENAT;
    sqe.user_data = 9;

    ASTIA_CHECK(ld_aio_submit(&backend, ring, &sqe) == 0);

    const ld_aio_bounds wait_bounds = {0, 3};

    fake.post_cancel = true;

    int timed_out = 0;

    ASTIA_CHECK(ld_aio_wait(&backend, ring, 9, &wait_bounds, &timed_out) == -ETIMEDOUT);
    ASTIA_CHECK(timed_out == 1);
    ASTIA_CHECK(ld_aio_cancel(&backend, ring, 9, 10, &ld_aio_cancel_bounds) == 0);
    ASTIA_CHECK(astia_cq_ready(ring) == 0);
}
