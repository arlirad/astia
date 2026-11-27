#include "error.h"

typedef struct {
    uint64_t key;
    int      set;
    char     text[LD_ERROR_TEXT_MAX];
} ld_error_slot;

static ld_error_slot g_slots[LD_ERROR_THREAD_SLOTS];
static char          g_shared_text[LD_ERROR_TEXT_MAX];
static int           g_shared_set;

/* Slots are claimed lock-free on the thread key, exactly like the DTV table:
 * the claiming thread publishes only its key, then fills its own slot.  A
 * full table returns no slot, so the caller degrades to the shared buffer
 * rather than dropping the error or failing. */
static ld_error_slot* slot_for(uint64_t thread_key) {
    if (thread_key == 0)
        return 0;

    for (int i = 0; i < LD_ERROR_THREAD_SLOTS; ++i) {
        if (__atomic_load_n(&g_slots[i].key, __ATOMIC_ACQUIRE) == thread_key)
            return &g_slots[i];
    }

    for (int i = 0; i < LD_ERROR_THREAD_SLOTS; ++i) {
        uint64_t expected = 0;

        if (__atomic_compare_exchange_n(&g_slots[i].key, &expected, thread_key, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return &g_slots[i];

        if (expected == thread_key)
            return &g_slots[i];
    }

    return 0;
}

/* Reads never claim a slot: a thread that has not set an error must not
 * consume one, and must not see the buffer of an unrelated thread. */
static ld_error_slot* slot_lookup(uint64_t thread_key) {
    if (thread_key == 0)
        return 0;

    for (int i = 0; i < LD_ERROR_THREAD_SLOTS; ++i) {
        if (__atomic_load_n(&g_slots[i].key, __ATOMIC_ACQUIRE) == thread_key)
            return &g_slots[i];
    }

    return 0;
}

static void append_text(char* destination, const char* source, unsigned long* size) {
    for (unsigned long i = 0; source[i] != '\0' && *size + 1 < LD_ERROR_TEXT_MAX; ++i) {
        destination[(*size)++] = source[i];
    }
}

static void store_text(uint64_t thread_key, const char* prefix, const char* text) {
    ld_error_slot* slot        = slot_for(thread_key);
    char*          destination = slot != 0 ? slot->text : g_shared_text;
    int*           set         = slot != 0 ? &slot->set : &g_shared_set;

    unsigned long size = 0;

    append_text(destination, prefix, &size);

    if (text != 0)
        append_text(destination, text, &size);

    destination[size] = '\0';

    __atomic_store_n(set, 1, __ATOMIC_RELEASE);
}

__attribute__((visibility("hidden"))) void ld_error_set(uint64_t    thread_key,
                                                        const char* text) {
    store_text(thread_key, text, 0);
}

__attribute__((visibility("hidden"))) void
ld_error_set_path(uint64_t thread_key, const char* prefix, const char* path) {
    store_text(thread_key, prefix, path);
}

__attribute__((visibility("hidden"))) char* ld_error_take(uint64_t thread_key) {
    ld_error_slot* slot = slot_lookup(thread_key);
    if (slot != 0) {
        if (!__atomic_load_n(&slot->set, __ATOMIC_ACQUIRE))
            return 0;

        __atomic_store_n(&slot->set, 0, __ATOMIC_RELEASE);

        return slot->text;
    }

    if (!__atomic_load_n(&g_shared_set, __ATOMIC_ACQUIRE))
        return 0;

    __atomic_store_n(&g_shared_set, 0, __ATOMIC_RELEASE);

    return g_shared_text;
}

__attribute__((visibility("hidden"))) void ld_error_reset(void) {
    for (int i = 0; i < LD_ERROR_THREAD_SLOTS; ++i) {
        __atomic_store_n(&g_slots[i].set, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&g_slots[i].key, 0, __ATOMIC_RELEASE);

        g_slots[i].text[0] = '\0';
    }

    g_shared_text[0] = '\0';

    __atomic_store_n(&g_shared_set, 0, __ATOMIC_RELEASE);
}
