#pragma once

/*
 * Bounded completion wait for the interpreter's one-operation AIO ring.
 *
 * The loader submits a single SQE, kicks it, and waits for the matching CQE
 * while dropping anything older.  A filesystem or device that accepts the
 * submission but never publishes a completion must not wedge the process, so
 * the wait is bounded twice: a monotonic deadline from the backend clock and,
 * because that clock can be missing or broken, an idle-poll backstop.  When
 * the bound is reached the caller gets -ETIMEDOUT and ld_aio_cancel() asks the
 * ring to cancel the operation and drain the two completions it produces.
 *
 * The ring primitives live behind ld_aio_backend so host tests can drive the
 * real SQE/CQE helpers against a fake ring without a kernel.
 */

#include <astia/api/aio.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ld_aio_backend {
    astia_sqe* (*sqe_slot)(astia_rings* ring);
    void (*sq_publish)(astia_rings* ring, uint32_t count);
    long (*submit)(astia_rings* ring, uint32_t count);
    long (*yield)(void);
    astia_cqe* (*cq_peek)(astia_rings* ring);
    void (*cq_consume)(astia_rings* ring, uint32_t count);
    /* Monotonic nanoseconds, or 0 when no clock is available. */
    uint64_t (*now_ns)(void);
} ld_aio_backend;

typedef struct ld_aio_bounds {
    /* Zero disables the clock deadline and leaves only max_polls. */
    uint64_t timeout_ns;
    uint32_t max_polls;
} ld_aio_bounds;

/* Long enough for any loader file access, short enough that one stuck device
   denies service to a single call instead of the whole process. */
#define LD_AIO_TIMEOUT_NS ((uint64_t) 5000000000ull)
#define LD_AIO_MAX_POLLS ((uint32_t) (1u << 20))

/* A cancel posts the target's -ECANCELED plus its own acknowledgement; give
   both a short, separately bounded chance to land. */
#define LD_AIO_CANCEL_TIMEOUT_NS ((uint64_t) 250000000ull)
#define LD_AIO_CANCEL_MAX_POLLS ((uint32_t) 4096)

extern const ld_aio_bounds ld_aio_wait_bounds;
extern const ld_aio_bounds ld_aio_cancel_bounds;

/* Fill, publish and kick one SQE.  Returns -EBUSY when the SQ stays full and
   the backend's error when the kick fails. */
long ld_aio_submit(const ld_aio_backend* backend, astia_rings* ring,
                   const astia_sqe* sqe);

/* Wait for `tag`, dropping stale completions.  Returns the matching CQE result
   with *timed_out cleared, -ETIMEDOUT with *timed_out set, or -EBUSY/backend
   error when the loop cannot make progress. */
long ld_aio_wait(const ld_aio_backend* backend, astia_rings* ring, uint64_t tag,
                 const ld_aio_bounds* bounds, int* timed_out);

/* Best-effort OP_CANCEL for `target`: submits the cancellation and drains the
   target completion and the cancellation acknowledgement within `bounds`.
   Returns the backend's submission error or 0. */
long ld_aio_cancel(const ld_aio_backend* backend, astia_rings* ring, uint64_t target,
                   uint64_t cancel_tag, const ld_aio_bounds* bounds);

#ifdef __cplusplus
}
#endif
