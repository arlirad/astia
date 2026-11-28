#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            fails++;                                               \
        }                                                          \
    } while (0)

static void test_alignment_and_content(void) {
    static const size_t sizes[] = {1, 15, 16, 17, 100, 1000, 4096};

    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        size_t n = sizes[i];

        unsigned char* p = malloc(n);

        CHECK(p != NULL);

        if (!p)
            continue;

        CHECK(((uintptr_t) p % 16) == 0);

        for (size_t j = 0; j < n; j++) {
            p[j] = (unsigned char) (j * 7 + 3);
        }

        for (size_t j = 0; j < n; j++) {
            if (p[j] != (unsigned char) (j * 7 + 3)) {
                CHECK(0);
                break;
            }
        }

        free(p);
    }
}

static void test_zero_and_null(void) {
    void* z1 = malloc(0);
    void* z2 = malloc(0);

    CHECK(z1 != NULL);
    CHECK(z2 != NULL);
    CHECK(z1 != z2); /* distinct pointers, both usable */
    free(z1);
    free(z2);
    free(NULL); /* no-op by definition */
    CHECK(1);
}

static void test_calloc(void) {
    unsigned char* c;

    volatile size_t huge = (size_t) -1; /* volatile: this test wants the
                                           runtime overflow check, not
                                           the optimizer's verdict on an
                                           impossible constant */
    volatile size_t zero = 0;

    c = calloc(64, 4);

    CHECK(c != NULL);

    if (c) {
        for (size_t i = 0; i < 256; i++) {
            if (c[i] != 0) {
                CHECK(0);
                break;
            }
        }

        free(c);
    }

    CHECK(calloc(huge, 4) == NULL); /* nmemb*size would wrap */
    CHECK(calloc(4, huge) == NULL);

    c = calloc(zero, 8);

    CHECK(c != NULL); /* malloc(0) semantics */
    free(c);
}

static void test_posix_memalign(void) {
    static const size_t alignments[] = {sizeof(void*), 16, 32, 4096};

    void* p;
    void* unchanged = (void*) (uintptr_t) 0x1234;

    for (size_t i = 0; i < sizeof alignments / sizeof alignments[0]; i++) {
        p = NULL;

        CHECK(posix_memalign(&p, alignments[i], 37) == 0);
        CHECK(p != NULL);

        if (p) {
            CHECK(((uintptr_t) p % alignments[i]) == 0);
            memset(p, 0xa5, 37);
            free(p);
        }
    }

    p = unchanged;

    CHECK(posix_memalign(&p, 0, 1) == EINVAL);
    CHECK(p == unchanged);
    CHECK(posix_memalign(&p, 3, 1) == EINVAL);
    CHECK(p == unchanged);
    CHECK(posix_memalign(&p, 24, 1) == EINVAL);
    CHECK(p == unchanged);
    CHECK(posix_memalign(NULL, sizeof(void*), 1) == EINVAL);

    p = unchanged;

    CHECK(posix_memalign(&p, sizeof(void*), (size_t) -1) == ENOMEM);
    CHECK(p == unchanged);
}

static void test_realloc(void) {
    char* p = realloc(NULL, 32); /* = malloc */

    CHECK(p != NULL);

    if (!p)
        return;

    memset(p, 'x', 32);

    p = realloc(p, 200); /* grow: relocate or extend */

    CHECK(p != NULL);

    if (!p)
        return;

    {
        int ok = 1;

        for (int i = 0; i < 32; i++) {
            if (p[i] != 'x')
                ok = 0;
        }

        CHECK(ok);
    }

    memset(p + 32, 'y', 168);

    p = realloc(p, 100); /* shrink: keep the head */

    CHECK(p != NULL);

    if (!p)
        return;

    {
        int ok = 1;

        for (int i = 0; i < 32; i++) {
            if (p[i] != 'x')
                ok = 0;
        }

        for (int i = 32; i < 100; i++) {
            if (p[i] != 'y')
                ok = 0;
        }

        CHECK(ok);
    }

    /* oversized request fails *without* destroying the old block — the
       standard's guarantee for a NULL realloc result.  gcc's use-after-
       free analysis cannot see that this particular call returns NULL,
       hence the local suppression: using q here IS the behavior under
       test. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuse-after-free"
    {
        char* q = malloc(64);

        CHECK(q != NULL);

        if (q) {
            strcpy(q, "keep");
            CHECK(realloc(q, 4u << 20) == NULL);
            CHECK(strcmp(q, "keep") == 0);
            free(q);
        }
    }
#pragma GCC diagnostic pop

    CHECK(realloc(p, 0) == NULL); /* frees, returns NULL */
}

static void test_realloc_memalign(void) {
    static const size_t alignments[] = {16, 32, 64, 256, 4096};

    for (size_t i = 0; i < sizeof alignments / sizeof alignments[0]; i++) {
        size_t alignment = alignments[i];

        unsigned char* p = NULL;
        unsigned char* guard;
        unsigned char* q;

        int ok;

        CHECK(posix_memalign((void**) &p, alignment, 64) == 0);
        CHECK(p != NULL);

        if (!p)
            continue;

        CHECK(((uintptr_t) p % alignment) == 0);

        for (size_t j = 0; j < 64; j++) {
            p[j] = (unsigned char) (j * 5 + 7);
        }

        guard = malloc(96);

        CHECK(guard != NULL);

        if (!guard) {
            free(p);

            continue;
        }

        for (size_t j = 0; j < 96; j++) {
            guard[j] = (unsigned char) (0xc0 | (j & 0x3f));
        }

        q = realloc(p, 4096);

        CHECK(q != NULL);

        if (!q) {
            free(p);
            free(guard);

            continue;
        }

        CHECK(((uintptr_t) q % alignment) == 0);

        ok = 1;

        for (size_t j = 0; j < 64; j++) {
            if (q[j] != (unsigned char) (j * 5 + 7))
                ok = 0;
        }

        CHECK(ok);

        ok = 1;

        for (size_t j = 0; j < 96; j++) {
            if (guard[j] != (unsigned char) (0xc0 | (j & 0x3f)))
                ok = 0;
        }

        CHECK(ok);

        {
            unsigned char* shrunk = realloc(q, 16);

            CHECK(shrunk != NULL);

            if (shrunk)
                q = shrunk;
        }

        CHECK(((uintptr_t) q % alignment) == 0);

        ok = 1;

        for (size_t j = 0; j < 16; j++) {
            if (q[j] != (unsigned char) (j * 5 + 7))
                ok = 0;
        }

        CHECK(ok);

        ok = 1;

        for (size_t j = 0; j < 96; j++) {
            if (guard[j] != (unsigned char) (0xc0 | (j & 0x3f)))
                ok = 0;
        }

        CHECK(ok);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuse-after-free"
        CHECK(realloc(q, 4u << 20) == NULL);
        CHECK(errno == ENOMEM);
        CHECK(((uintptr_t) q % alignment) == 0);

        ok = 1;

        for (size_t j = 0; j < 16; j++) {
            if (q[j] != (unsigned char) (j * 5 + 7))
                ok = 0;
        }

        CHECK(ok);
#pragma GCC diagnostic pop

        free(q);
        free(guard);
    }

    {
        void* p = NULL;

        CHECK(posix_memalign(&p, 64, 32) == 0);

        if (p) {
            CHECK(realloc(p, 0) == NULL);

            p = malloc(64);

            CHECK(p != NULL);
            free(p);
        }
    }
}

static void test_coalesce(void) {
    enum { N = 64 };

    void* ps[N];
    void* big;
    void* a1;
    void* a2;
    void* a3;

    /* drain the arena completely, then free: the 64 freed 8 KiB blocks
       were adjacent, so one 512 KiB request succeeding proves they
       coalesced — first-fit over un-merged 8 KiB leftovers must fail */
    for (int i = 0; i < N; i++) {
        ps[i] = malloc(8 * 1024);

        CHECK(ps[i] != NULL);
    }

    for (int i = 0; i < N; i++) {
        free(ps[i]);
    }

    big = malloc(512 * 1024);

    CHECK(big != NULL);
    free(big);

    /* explicit three-block merge: a1 | a2 | a3 with a2 freed last */
    a1 = malloc(1024);
    a2 = malloc(1024);
    a3 = malloc(1024);

    CHECK(a1 != NULL && a2 != NULL && a3 != NULL);
    free(a1);
    free(a3);
    free(a2);

    /* merged payload is 3*1024 + 2 headers = 3168 > 3100 */
    big = malloc(3100);

    CHECK(big != NULL);
    free(big);
}

static void test_exhaustion(void) {
    enum { CHUNK = 65536, MAXC = 64 };

    void* chunks[MAXC];

    int n = 0;

    CHECK(malloc(2u << 20) == NULL); /* larger than the arena */

    for (; n < MAXC; n++) {
        chunks[n] = malloc(CHUNK);

        if (chunks[n] == NULL)
            break;
    }

    CHECK(n > 0);    /* it served something */
    CHECK(n < MAXC); /* ...but ran out */

    for (int i = 0; i < n; i++) {
        free(chunks[i]);
    }

    {
        void* p = malloc(CHUNK); /* whole arena is back */

        CHECK(p != NULL);
        free(p);
    }
}

static void test_errno(void) {
    enum { CHUNK = 65536, MAXC = 64 };

    void* chunks[MAXC];

    int n = 0;

    volatile size_t huge = (size_t) -1;

    errno = EACCES;

    {
        void* p = malloc(64);

        CHECK(p != NULL);
        CHECK(errno == EACCES); /* success leaves errno alone */
        free(p);
    }

    errno = 0;

    CHECK(malloc(2u << 20) == NULL);
    CHECK(errno == ENOMEM);

    errno = 0;

    CHECK(calloc(huge, 4) == NULL);
    CHECK(errno == ENOMEM); /* multiplication overflow */

    errno = 0;

    CHECK(calloc(1u << 20, 4) == NULL);
    CHECK(errno == ENOMEM); /* allocation failure without overflow */

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuse-after-free"
    {
        char* q = malloc(64);

        CHECK(q != NULL);

        if (q) {
            strcpy(q, "keep");

            errno = 0;

            CHECK(realloc(q, 2u << 20) == NULL);
            CHECK(errno == ENOMEM);
            CHECK(strcmp(q, "keep") == 0); /* the old block survives */
            free(q);
        }
    }
#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuse-after-free"
    {
        char* r = malloc(64);

        CHECK(r != NULL);

        if (r) {
            strcpy(r, "alive");

            for (; n < MAXC; n++) {
                chunks[n] = malloc(CHUNK);

                if (chunks[n] == NULL)
                    break;
            }

            CHECK(n > 0);
            CHECK(n < MAXC);

            errno = 0;

            CHECK(realloc(r, 256 * 1024) == NULL);
            CHECK(errno == ENOMEM);
            CHECK(strcmp(r, "alive") == 0); /* relocation failed cleanly */

            free(r);

            for (int i = 0; i < n; i++) {
                free(chunks[i]);

                chunks[i] = NULL;
            }
        }
    }
#pragma GCC diagnostic pop

    {
        void* p = (void*) (uintptr_t) 0x1234;

        errno = EACCES;

        CHECK(posix_memalign(&p, sizeof(void*), (size_t) -1) == ENOMEM);
        CHECK(errno == EACCES); /* posix_memalign must not touch errno */
    }

    {
        char* p = realloc(NULL, 32);

        CHECK(p != NULL);

        if (p) {
            errno = EACCES;

            p = realloc(p, 64);

            CHECK(p != NULL);
            CHECK(errno == EACCES); /* success leaves errno alone */
            free(p);
        }
    }

    {
        void* p = malloc(CHUNK); /* the whole arena is back */

        CHECK(p != NULL);
        free(p);
    }
}

static void test_stress(void) {
    enum { SLOTS = 128, ITERS = 3000 };

    static unsigned char* slots[SLOTS];

    static size_t sizes[SLOTS];
    int           rng = 1;

    for (int it = 0; it < ITERS; it++) {
        int idx;

        rng = rng * 1103515245 + 12345;
        idx = (int) ((unsigned) rng / 65536u) % SLOTS;

        if (slots[idx]) { /* verify, then recycle */
            unsigned char* p = slots[idx];

            int ok = 1;

            for (size_t j = 0; j < sizes[idx]; j++) {
                if (p[j] != (unsigned char) (idx * 31u + j))
                    ok = 0;
            }

            CHECK(ok);
            free(p);

            slots[idx] = NULL;
        }
        else {
            size_t sz = 1 + (size_t) ((unsigned) rng % 4096u);

            unsigned char* p = malloc(sz);

            CHECK(p != NULL);

            if (!p)
                continue;

            CHECK(((uintptr_t) p % 16) == 0);

            for (size_t j = 0; j < sz; j++) {
                p[j] = (unsigned char) (idx * 31u + j);
            }

            slots[idx] = p;
            sizes[idx] = sz;
        }
    }

    for (int i = 0; i < SLOTS; i++) { /* drain: one last verify */
        if (!slots[i])
            continue;

        {
            int ok = 1;

            for (size_t j = 0; j < sizes[i]; j++) {
                if (slots[i][j] != (unsigned char) (i * 31u + j))
                    ok = 0;
            }

            CHECK(ok);
        }

        free(slots[i]);

        slots[i] = NULL;
    }
}

int main(void) {
    test_alignment_and_content();
    test_zero_and_null();
    test_calloc();
    test_posix_memalign();
    test_realloc();
    test_realloc_memalign();
    test_coalesce(); /* needs a drained arena */
    test_exhaustion();
    test_errno();
    test_stress();

    if (fails)
        printf("malloc_test: %d failure(s)\n", fails);

    return fails != 0;
}
