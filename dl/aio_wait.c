#include "aio_wait.h"

#include <astia/api/errno.h>

const ld_aio_bounds ld_aio_wait_bounds   = {LD_AIO_TIMEOUT_NS, LD_AIO_MAX_POLLS};
const ld_aio_bounds ld_aio_cancel_bounds = {LD_AIO_CANCEL_TIMEOUT_NS,
                                            LD_AIO_CANCEL_MAX_POLLS};

/* A wait budget is consumed only while there is nothing to reap, so a pending
   completion costs no clock read and a full CQ cannot spin the loop. */
typedef struct ld_aio_budget {
    const ld_aio_backend* backend;
    uint64_t              started;
    uint64_t              timeout_ns;
    uint32_t              max_polls;
    uint32_t              polls;
} ld_aio_budget;

static void budget_init(ld_aio_budget* budget, const ld_aio_backend* backend,
                        const ld_aio_bounds* bounds) {
    budget->backend    = backend;
    budget->started    = 0;
    budget->timeout_ns = bounds->timeout_ns;
    budget->max_polls  = bounds->max_polls;
    budget->polls      = 0;
}

/* Counts one idle poll and reports whether either bound is reached. */
static int budget_exhausted(ld_aio_budget* budget) {
    if (budget->polls >= budget->max_polls)
        return 1;

    ++budget->polls;

    if (budget->timeout_ns == 0)
        return 0;

    const uint64_t now = budget->backend->now_ns();
    if (now == 0)
        return 0;

    if (budget->started == 0) {
        budget->started = now;

        return 0;
    }

    return now - budget->started >= budget->timeout_ns;
}

static long budget_timeout(int* timed_out) {
    *timed_out = 1;

    return -ETIMEDOUT;
}

long ld_aio_submit(const ld_aio_backend* backend, astia_rings* ring,
                   const astia_sqe* sqe) {
    astia_sqe* slot = backend->sqe_slot(ring);
    if (slot == NULL) {
        const long rc = backend->submit(ring, 0);
        if (rc < 0)
            return rc;

        slot = backend->sqe_slot(ring);
    }

    if (slot == NULL)
        return -EBUSY;

    *slot = *sqe;

    backend->sq_publish(ring, 1);

    return backend->submit(ring, 1);
}

long ld_aio_wait(const ld_aio_backend* backend, astia_rings* ring, uint64_t tag,
                 const ld_aio_bounds* bounds, int* timed_out) {
    ld_aio_budget budget;

    *timed_out = 0;

    budget_init(&budget, backend, bounds);

    for (;;) {
        astia_cqe* completion = backend->cq_peek(ring);
        if (completion != NULL) {
            const long result = completion->result;
            const int  mine   = completion->user_data == tag;

            backend->cq_consume(ring, 1);

            if (mine)
                return result;

            if (budget_exhausted(&budget))
                return budget_timeout(timed_out);

            continue;
        }

        if (budget_exhausted(&budget))
            return budget_timeout(timed_out);

        const long rc = backend->submit(ring, 0);
        if (rc < 0)
            return rc;

        (void) backend->yield();
    }
}

long ld_aio_cancel(const ld_aio_backend* backend, astia_rings* ring, uint64_t target,
                   uint64_t cancel_tag, const ld_aio_bounds* bounds) {
    astia_sqe     sqe    = {0};
    ld_aio_budget budget = {0};

    int target_seen = 0;
    int cancel_seen = 0;

    sqe.op        = OP_CANCEL;
    sqe.offset    = target;
    sqe.user_data = cancel_tag;

    const long rc = ld_aio_submit(backend, ring, &sqe);

    if (rc < 0)
        return rc;

    budget_init(&budget, backend, bounds);

    while (target_seen == 0 || cancel_seen == 0) {
        astia_cqe* completion = backend->cq_peek(ring);
        if (completion == NULL) {
            if (budget_exhausted(&budget))
                break;

            (void) backend->yield();

            continue;
        }

        const uint64_t id = completion->user_data;

        backend->cq_consume(ring, 1);

        if (id == target)
            target_seen = 1;
        else if (id == cancel_tag)
            cancel_seen = 1;
    }

    return 0;
}
