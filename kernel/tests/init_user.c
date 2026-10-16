/*
 * First userspace image: the userspace ABI in one program.
 *
 * It crosses ring 3 through <astia/api/syscalls.h> (direct calls) and
 * <astia/api/aio.h> (the async rings), and reports the step it reached
 * as its exit status — 0 means every check passed. The boot test greps
 * for the kernel's "user thread exit status=0" line, so a failure at any
 * numbered step fails the test with the number in the serial log:
 *
 *   1  getpid
 *   2-5   clock_gettime: read, range, wall-clock origin, no regression
 *   6-7   io_setup + the ring's magic read *from ring 3* — proof that
 *         the ring is mapped user-accessible rather than kernel-only
 *   8-10  OPENAT /sbin/init through the ring
 *   11-12 STATX in fstat form (address NULL) on that descriptor
 *   13-14 STATX in path form, same size as the fstat reading
 *   15    CLOSE
 *   16-17 io_enter waits that must run out of completions: nothing to
 *         reap, then a 10 ms budget that has to come back -ETIMEDOUT
 *   18    io_destroy
 *   19    the startup frame proc::spawnve built on the entry stack:
 *         argc/argv/envp exactly as the kernel was handed them
 *         (checked first, numbered last)
 *   20    user-pointer validation: kernel-half and unmapped addresses
 *         must come back -EFAULT, never be touched
 *   21    spawnve: bad path/vector/element pointers come back
 *         -ENOENT/-E2BIG/-EFAULT without a trace, and the fixture's own
 *         process (every boot's first spawn, pid 1) spawns and reaps
 *         one full copy of itself — children skip that case or they
 *         would recurse forever
 */

#include <astia/api/aio.h>
#include <astia/api/syscalls.h>

static astia_sqe* fill(astia_rings* ring, uint32_t op, int32_t fd, uint64_t address,
                       uint64_t offset, uint32_t length, uint64_t tag) {
    astia_sqe* s = astia_sqe_slot(ring);
    if (s == NULL)
        return NULL;
    *s           = (astia_sqe) {0};
    s->op        = op;
    s->fd        = fd;
    s->address   = address;
    s->offset    = offset;
    s->length    = length;
    s->user_data = tag;
    return s;
}

/* Publish the filled slot, kick it, and return the completion's result.
   The budget only bounds a completion that is not already there: every
   operation below completes inline in the dispatch. */
static long kick(astia_rings* ring, uint64_t tag, long* rc_out) {
    astia_timespec budget;
    astia_cqe*     c;

    astia_sq_publish(ring, 1);
    budget.tv_sec  = 0;
    budget.tv_nsec = 100000000L; /* 100 ms */
    *rc_out        = astia_io_enter(ring, 1, 1, ENTER_GETEVENTS, &budget);
    if (*rc_out < 0)
        return *rc_out;
    c = astia_cq_peek(ring);
    if (c == NULL || c->user_data != tag) {
        *rc_out = -1; /* kick ran but no completion for us */
        return -1;
    }
    astia_cq_consume(ring, 1); /* reap it: advance head past this one */
    *rc_out = 0;
    return c->result;
}

static int streq(const char* a, const char* b) {
    while (*a != '\0' && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static int prefix(const char* text, const char* stem) {
    while (*stem != '\0' && *text == *stem) {
        ++text;
        ++stem;
    }
    return *stem == '\0';
}

void c_main(const long* frame) {
    const long   argc = frame[0];
    const char** argv = (const char**) &frame[1];
    const char** envp = &argv[argc + 1];

    /* --- the startup frame the kernel built ------------------------- */
    if (argc != 2 || argv[0] == NULL || argv[1] == NULL || argv[2] != NULL)
        astia_exit(19);
    if (!streq(argv[0], "/sbin/init") || !streq(argv[1], "--flag"))
        astia_exit(19);
    if (envp[0] == NULL || envp[1] != NULL || !prefix(envp[0], "HOME="))
        astia_exit(19);

    astia_rings*   ring = NULL;
    astia_stat     st;
    astia_stat     st_path;
    astia_timespec first;
    astia_timespec second;
    astia_timespec wall;
    astia_timespec budget;
    long           rc;
    long           fd;
    uint64_t       tag = 1;

    if (astia_getpid() < 0)
        astia_exit(1);

    /* --- direct calls: the clock ------------------------------------ */
    if (astia_clock_gettime(CLOCK_MONOTONIC, &first) != 0)
        astia_exit(2);
    if (first.tv_nsec < 0 || first.tv_nsec >= 1000000000LL)
        astia_exit(3);
    if (astia_clock_gettime(CLOCK_REALTIME, &wall) != 0 || wall.tv_sec < 1000000000LL)
        astia_exit(4);
    if (astia_clock_gettime(CLOCK_MONOTONIC, &second) != 0)
        astia_exit(5);
    if (second.tv_sec < first.tv_sec ||
        (second.tv_sec == first.tv_sec && second.tv_nsec < first.tv_nsec))
        astia_exit(5);

    /* --- the ring must be *our* memory ------------------------------ */
    if (astia_io_setup(8, 0, &ring) != 0 || ring == NULL)
        astia_exit(6);
    if (ring->magic != RINGS_MAGIC)
        astia_exit(7);

    /* --- OPENAT /sbin/init ------------------------------------------ */
    if (fill(ring, OP_OPENAT, -1, (uint64_t) (uintptr_t) "/sbin/init", 0, 0, tag) == NULL)
        astia_exit(8);
    fd = kick(ring, (uint64_t) tag++, &rc);
    if (rc < 0)
        astia_exit(9);
    if (fd < 0)
        astia_exit(10);

    /* --- STATX, fstat form: address NULL, fd is the target ---------- */
    st.size = 0;
    if (fill(ring, OP_STATX, (int32_t) fd, 0, (uint64_t) (uintptr_t) &st,
             (uint32_t) sizeof st, tag) == NULL)
        astia_exit(11);
    rc = kick(ring, (uint64_t) tag++, &(long) {0});
    if (rc != 0 || st.size == 0)
        astia_exit(12);

    /* --- STATX, path form: address is the pathname ------------------ */
    if (fill(ring, OP_STATX, -1, (uint64_t) (uintptr_t) "/sbin/init",
             (uint64_t) (uintptr_t) &st_path, (uint32_t) sizeof st_path, tag) == NULL)
        astia_exit(13);
    rc = kick(ring, (uint64_t) tag++, &(long) {0});
    if (rc != 0 || st_path.size != st.size)
        astia_exit(14);

    /* --- CLOSE ------------------------------------------------------- */
    if (fill(ring, OP_CLOSE, (int32_t) fd, 0, 0, 0, tag) == NULL)
        astia_exit(15);
    rc = kick(ring, (uint64_t) tag++, &(long) {0});
    if (rc != 0)
        astia_exit(15);

    /* --- waiting must run out, not hang: nothing to reap, then a
           10 ms budget that has to expire as -ETIMEDOUT --------------- */
    if (astia_cq_peek(ring) != NULL)
        astia_exit(16);
    budget.tv_sec  = 0;
    budget.tv_nsec = 10000000L;
    if (astia_io_enter(ring, 0, 1, ENTER_GETEVENTS, &budget) != -ETIMEDOUT)
        astia_exit(17);

    if (astia_io_destroy(ring) != 0)
        astia_exit(18);

    /* --- user pointers are validated, not trusted ------------------- */
    /* A kernel-half address: the process map shares the kernel half, so
       without validation this would write the exit status into kernel
       memory.  An unmapped user page: rejected the same way. */
    if (astia_waitpid(-1, (int32_t*) 0xFFFFFFFF80000000ull) != -EFAULT)
        astia_exit(20);
    if (astia_clock_gettime(CLOCK_MONOTONIC, (astia_timespec*) 0x1234) != -EFAULT)
        astia_exit(20);

    /* --- spawnve: the three arguments are validated user pointers --- */
    if (astia_spawnve("/sbin/no-such-image", NULL, NULL) != -ENOENT)
        astia_exit(21);
    if (astia_spawnve((const char*) 0xFFFFFFFF80000000ull, NULL, NULL) != -EFAULT)
        astia_exit(21);
    if (astia_spawnve("/sbin/init", (char* const*) 0xFFFFFFFF80000000ull, NULL) !=
        -EFAULT)
        astia_exit(21);
    if (astia_spawnve("/sbin/init", (char* const*) 0x1234ull, NULL) != -EFAULT)
        astia_exit(21);
    {
        char* bad_element[] = {(char*) 0xFFFFFFFF80000000ull, NULL};
        if (astia_spawnve("/sbin/init", bad_element, NULL) != -EFAULT)
            astia_exit(21);
    }
    /* an unterminated vector is cut off with -E2BIG, never walked */
    {
        char* oversized[33];
        for (int i = 0; i < 33; ++i)
            oversized[i] = (char*) "/sbin/init";
        if (astia_spawnve("/sbin/init", oversized, NULL) != -E2BIG)
            astia_exit(21);
    }
    /* One real child — but only from the fixture's own process: every
       boot's first spawn is pid 1, and the child re-runs every step
       including this one, so without the guard each child would spawn
       itself forever.  argv/envp are exactly what step 19 demands. */
    if (astia_getpid() == 1) {
        char*   child_argv[] = {(char*) "/sbin/init", (char*) "--flag", NULL};
        char*   child_envp[] = {(char*) "HOME=/", NULL};
        long    child        = astia_spawnve("/sbin/init", child_argv, child_envp);
        int32_t child_status = -1;
        if (child <= 0)
            astia_exit(21);
        if (astia_waitpid((int32_t) child, &child_status) != child || child_status != 0)
            astia_exit(21);
    }

    astia_exit(0);
    for (;;) {
        __asm__ volatile("hlt");
    }
}

/* The only arch-specific part: at entry %rsp points at argc, so hand
   the unmodified frame to C before any prologue moves the stack. */
__asm__(".globl _start\n"
        "_start:\n"
        "    movq %rsp, %rdi\n"
        "    call c_main\n"
        "    ud2\n");
