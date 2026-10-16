/*
 * errno must be per-thread storage — that is the contract written into
 * <errno.h> from day one, and this is the test that would fail if it
 * were ever a process global again.
 *
 * Host pthreads are used purely as a thread source; the object under
 * test is declared directly (not via <errno.h>) so this file cannot be
 * swayed by whichever errno macro the host's headers happen to define.
 */
/* This test deliberately uses the host's pthread implementation as a
 * thread source.  The shipping libc now has its own <pthread.h>, so
 * skip that public header here and retain the host ABI for this test. */
#if defined(__GNUC__)
#include_next <pthread.h>
#else
#include <pthread.h>
#endif
#include <stdio.h>

extern _Thread_local int __astia_errno;    /* <errno.h>'s object */

static int fails;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            fails++;                                                    \
        }                                                               \
    } while (0)

static void* setter(void* arg)
{
    int want = (int) (long) arg;

    __astia_errno = want;
    if (__astia_errno != want)
        return (void*) 1;    /* did not keep its own value */
    return NULL;
}

int main(void)
{
    pthread_t t;
    void* status = NULL;

    __astia_errno = 7;

    if (pthread_create(&t, NULL, setter, (void*) 111L) != 0) {
        printf("FAIL: pthread_create\n");
        return 1;
    }
    pthread_join(t, &status);

    CHECK(status == NULL);       /* the thread saw its own write ... */
    CHECK(__astia_errno == 7);   /* ... and it did not touch ours */

    __astia_errno = 0;           /* leave the slot pristine */
    if (fails)
        printf("errno_test: %d failure(s)\n", fails);
    return fails != 0;
}
