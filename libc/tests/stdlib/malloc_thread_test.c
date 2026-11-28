/* This test deliberately uses the host's pthread implementation as a
 * thread source, the same way the errno test does: the allocator under
 * test is shipping astia code, while host pthreads only create threads. */
#if defined(__GNUC__)
#include_next <pthread.h>
#else
#include <pthread.h>
#endif

#include <sys/wait.h>

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
    worker_count      = 8,
    worker_slots      = 4,
    worker_iterations = 4000,
    block_size_max    = 4096,
    fork_blocks       = 16,
    fork_extra_blocks = 32,
};

struct allocation {
    unsigned char* data;
    size_t         size;
    uint32_t       pattern;
};

static _Atomic int worker_failures;
static _Atomic int worker_ready;
static _Atomic int worker_stop;

static void worker_fail(void) {
    atomic_fetch_add_explicit(&worker_failures, 1, memory_order_relaxed);
}

static uint32_t random_next(uint32_t* state) {
    uint32_t value = *state;

    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;

    return value;
}

static unsigned char allocation_byte(uint32_t pattern, size_t index) {
    return (unsigned char) (pattern + (uint32_t) index * 31u + 7u);
}

static int allocation_valid(const struct allocation* allocation) {
    for (size_t index = 0; index < allocation->size; ++index) {
        if (allocation->data[index] != allocation_byte(allocation->pattern, index))
            return 0;
    }

    return 1;
}

static void allocation_fill(struct allocation* allocation) {
    for (size_t index = 0; index < allocation->size; ++index) {
        allocation->data[index] = allocation_byte(allocation->pattern, index);
    }
}

static void* stress_worker(void* argument) {
    const unsigned int id = (unsigned int) (uintptr_t) argument;

    struct allocation slots[worker_slots];
    uint32_t          random = 0x9e3779b9u ^ (id * 2654435761u);

    unsigned int iteration;

    memset(slots, 0, sizeof slots);

    atomic_fetch_add_explicit(&worker_ready, 1, memory_order_release);

    for (iteration = 0; iteration < worker_iterations; ++iteration) {
        const uint32_t     value = random_next(&random);
        struct allocation* slot  = &slots[value % worker_slots];

        if (slot->data == NULL) {
            size_t size = 1 + random_next(&random) % block_size_max;

            slot->pattern = random ^ (id << 24) ^ (iteration + 1u);
            slot->data    = malloc(size);

            if (slot->data == NULL) {
                worker_fail();

                continue;
            }

            slot->size = size;

            allocation_fill(slot);
        }
        else if ((value & 3u) == 0) {
            const size_t old_size = slot->size;
            const size_t new_size = 1 + random_next(&random) % (block_size_max * 2);

            if (!allocation_valid(slot))
                worker_fail();

            unsigned char* grown = realloc(slot->data, new_size);
            if (grown == NULL) {
                worker_fail();

                continue;
            }

            slot->data = grown;

            for (size_t index = 0; index < (old_size < new_size ? old_size : new_size);
                 ++index) {
                if (slot->data[index] != allocation_byte(slot->pattern, index)) {
                    worker_fail();

                    break;
                }
            }

            slot->size = new_size;

            allocation_fill(slot);
        }
        else {
            if (!allocation_valid(slot))
                worker_fail();

            free(slot->data);

            slot->data = NULL;
            slot->size = 0;
        }
    }

    for (unsigned int index = 0; index < worker_slots; ++index) {
        if (slots[index].data == NULL)
            continue;

        if (!allocation_valid(&slots[index]))
            worker_fail();

        free(slots[index].data);
    }

    return NULL;
}

static void* churn_worker(void* argument) {
    size_t index = (size_t) (uintptr_t) argument;

    atomic_fetch_add_explicit(&worker_ready, 1, memory_order_release);

    while (atomic_load_explicit(&worker_stop, memory_order_relaxed) == 0) {
        size_t size = 1 + index % 2048;

        void* data = malloc(size);

        if (data == NULL) {
            worker_fail();

            continue;
        }

        memset(data, 0x5a, size);
        free(data);

        ++index;
    }

    return NULL;
}

static void fill_fork_blocks(struct allocation* blocks) {
    for (unsigned int index = 0; index < fork_blocks; ++index) {
        size_t size = 128 + (size_t) index * 64;

        blocks[index].data    = malloc(size);
        blocks[index].pattern = 0x51ed270bu ^ (index * 2654435761u);

        if (blocks[index].data == NULL) {
            blocks[index].size = 0;

            worker_fail();

            continue;
        }

        blocks[index].size = size;

        allocation_fill(&blocks[index]);
    }
}

static int fork_blocks_valid(const struct allocation* blocks) {
    int valid = 1;

    for (unsigned int index = 0; index < fork_blocks; ++index) {
        if (blocks[index].data != NULL && !allocation_valid(&blocks[index]))
            valid = 0;
    }

    return valid;
}

static void release_fork_blocks(struct allocation* blocks) {
    for (unsigned int index = 0; index < fork_blocks; ++index) {
        free(blocks[index].data);

        blocks[index].data = NULL;
        blocks[index].size = 0;
    }
}

static int fork_child(const struct allocation* blocks) {
    struct allocation extra[fork_extra_blocks];

    if (!fork_blocks_valid(blocks))
        return 0;

    memset(extra, 0, sizeof extra);

    for (unsigned int index = 0; index < fork_extra_blocks; ++index) {
        size_t size = 16 + (size_t) index * 32;

        extra[index].data    = malloc(size);
        extra[index].pattern = 0xa5a5a5a5u ^ index;

        if (extra[index].data == NULL)
            return 0;

        extra[index].size = size;

        allocation_fill(&extra[index]);

        if (!allocation_valid(&extra[index]))
            return 0;
    }

    for (unsigned int index = 0; index < fork_extra_blocks; ++index) {
        free(extra[index].data);
    }

    return 1;
}

static int test_fork_after_malloc(void) {
    struct allocation blocks[fork_blocks];

    int status = 0;

    pid_t child;

    fill_fork_blocks(blocks);

    if (!fork_blocks_valid(blocks)) {
        release_fork_blocks(blocks);

        return 0;
    }

    child = fork();
    if (child < 0) {
        release_fork_blocks(blocks);

        return 0;
    }

    if (child == 0)
        _Exit(fork_child(blocks) ? 0 : 1);

    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0) {
        release_fork_blocks(blocks);

        return 0;
    }

    if (!fork_blocks_valid(blocks)) {
        release_fork_blocks(blocks);

        return 0;
    }

    release_fork_blocks(blocks);

    return 1;
}

static int test_fork_under_contention(void) {
    pthread_t         threads[worker_count];
    struct allocation blocks[fork_blocks];

    int status  = 0;
    int created = 0;

    pid_t child;

    int valid = 1;

    atomic_store_explicit(&worker_ready, 0, memory_order_relaxed);
    atomic_store_explicit(&worker_stop, 0, memory_order_relaxed);

    for (unsigned int index = 0; index < worker_count; ++index) {
        if (pthread_create(&threads[index], NULL, churn_worker,
                           (void*) (uintptr_t) index) != 0) {
            valid = 0;

            break;
        }

        ++created;
    }

    while (atomic_load_explicit(&worker_ready, memory_order_acquire) < created) {
    }

    fill_fork_blocks(blocks);

    child = fork();
    if (child == 0)
        _Exit(fork_child(blocks) ? 0 : 1);

    if (child < 0 || waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0)
        valid = 0;

    if (!fork_blocks_valid(blocks))
        valid = 0;

    release_fork_blocks(blocks);

    atomic_store_explicit(&worker_stop, 1, memory_order_release);

    for (int index = 0; index < created; ++index) {
        if (pthread_join(threads[index], NULL) != 0)
            valid = 0;
    }

    return valid;
}

int main(void) {
    pthread_t threads[worker_count];

    int created  = 0;
    int failures = 0;

    atomic_store_explicit(&worker_failures, 0, memory_order_relaxed);
    atomic_store_explicit(&worker_ready, 0, memory_order_relaxed);

    for (unsigned int index = 0; index < worker_count; ++index) {
        if (pthread_create(&threads[index], NULL, stress_worker,
                           (void*) (uintptr_t) index) != 0) {
            printf("FAIL: pthread_create\n");
            ++failures;

            break;
        }

        ++created;
    }

    for (int index = 0; index < created; ++index) {
        if (pthread_join(threads[index], NULL) != 0) {
            printf("FAIL: pthread_join\n");
            ++failures;
        }
    }

    if (!test_fork_after_malloc()) {
        printf("FAIL: fork after malloc\n");
        ++failures;
    }

    if (!test_fork_under_contention()) {
        printf("FAIL: fork with allocating threads\n");
        ++failures;
    }

    failures += atomic_load_explicit(&worker_failures, memory_order_relaxed);
    if (failures != 0)
        printf("malloc_thread_test: %d failure(s)\n", failures);

    return failures != 0;
}
