#include "atfork.h"

#include "process_port.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>

enum { pthread_atfork_max = 16384 };

struct pthread_atfork_record {
    void (*prepare)(void);
    void (*parent)(void);
    void (*child)(void);
    struct pthread_atfork_record*          previous;
    _Atomic(struct pthread_atfork_record*) next;
};

static atomic_flag                   pthread_atfork_lock = ATOMIC_FLAG_INIT;
static struct pthread_atfork_record  pthread_atfork_records[pthread_atfork_max];
static struct pthread_atfork_record* pthread_atfork_first;
static struct pthread_atfork_record* pthread_atfork_last;
static size_t                        pthread_atfork_count;

static void pthread_atfork_lock_acquire(void) {
    while (
        atomic_flag_test_and_set_explicit(&pthread_atfork_lock, memory_order_acquire)) {
        (void) __astia_process_yield();
    }
}

static void pthread_atfork_lock_release(void) {
    atomic_flag_clear_explicit(&pthread_atfork_lock, memory_order_release);
}

static void pthread_atfork_snapshot(struct astia_atfork_context* context) {
    pthread_atfork_lock_acquire();

    context->first = pthread_atfork_first;
    context->last  = pthread_atfork_last;
    context->count = pthread_atfork_count;

    pthread_atfork_lock_release();
}

int pthread_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void)) {
    struct pthread_atfork_record* record;

    pthread_atfork_lock_acquire();

    if (pthread_atfork_count == pthread_atfork_max) {
        pthread_atfork_lock_release();

        return ENOMEM;
    }

    record           = &pthread_atfork_records[pthread_atfork_count++];
    record->prepare  = prepare;
    record->parent   = parent;
    record->child    = child;
    record->previous = pthread_atfork_last;

    atomic_store_explicit(&record->next, NULL, memory_order_relaxed);

    if (pthread_atfork_last != NULL)
        atomic_store_explicit(&pthread_atfork_last->next, record, memory_order_release);
    else
        pthread_atfork_first = record;

    pthread_atfork_last = record;

    pthread_atfork_lock_release();

    return 0;
}

void __astia_atfork_prepare(struct astia_atfork_context* context) {
    pthread_atfork_snapshot(context);

    for (struct pthread_atfork_record* record = context->last; record != NULL;
         record                               = record->previous) {
        if (record->prepare != NULL)
            record->prepare();
    }
}

static void pthread_atfork_run_forward(const struct astia_atfork_context* context,
                                       void (*member)(struct pthread_atfork_record*)) {
    struct pthread_atfork_record* record = context->first;

    for (size_t remaining = context->count; remaining != 0; --remaining) {
        struct pthread_atfork_record* next =
            atomic_load_explicit(&record->next, memory_order_acquire);

        member(record);

        record = next;
    }
}

static void pthread_atfork_run_parent(struct pthread_atfork_record* record) {
    if (record->parent != NULL)
        record->parent();
}

static void pthread_atfork_run_child(struct pthread_atfork_record* record) {
    if (record->child != NULL)
        record->child();
}

void __astia_atfork_parent(const struct astia_atfork_context* context) {
    pthread_atfork_run_forward(context, pthread_atfork_run_parent);
}

void __astia_atfork_child(const struct astia_atfork_context* context) {
    pthread_atfork_run_forward(context, pthread_atfork_run_child);
}

void __astia_atfork_abort(struct astia_atfork_context* context) {
    context->first = NULL;
    context->last  = NULL;
    context->count = 0;
}
