#include "internal/allocator.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * astia libc — allocator v1
 *
 * A first-fit free list with boundary tags over ONE static arena: no
 * syscall, no brk, no mmap, nothing to link against.  This is the
 * deliberate v1 answer — when userspace can map memory, the arena gives
 * way to an mmap-backed MORECORE and this file barely changes.
 *
 * Layout: every block is [header | payload].  Headers carry the block
 * size and are linked to their *address* neighbours (prev/next), which
 * makes coalescing a pair of neighbour tests; free blocks are
 * additionally threaded through a first-fit list (fd/bk).  All payloads
 * are 16-byte aligned.
 *
 * Threading: one internal recursive spin lock owns the arena and both
 * lists.  It is statically initialized from zero and identifies its owner
 * through a thread-local address, so it works before pthread
 * initialization, during constructors, and while libc is bootstrapping.
 * fork() acquires it in prepare and resets it in the child, so the child
 * inherits a consistent arena and a usable allocator.  Like every malloc
 * with a lock, it is not async-signal-safe: a handler that interrupts the
 * lock holder re-enters the recursive path and must not touch the arena.
 */

#define ARENA_SIZE (1u << 20) /* 1 MiB of BSS: costs no file space */
#define ALIGN_MIN 16u
#define ALIGN_UP(x) (((x) + (size_t) (ALIGN_MIN - 1)) & ~(size_t) (ALIGN_MIN - 1))

struct blk {
    size_t      size; /* payload bytes, a multiple of ALIGN_MIN */
    int         free; /* on the free list? */
    struct blk* prev; /* address-order neighbours ... */
    struct blk* next;
    struct blk* fd; /* ... and free-list links (free blocks only) */
    struct blk* bk;
};

struct aligned_meta {
    size_t magic;
    void*  base;
    void*  user;
    size_t alignment;
};

#define HDR sizeof(struct blk)

_Static_assert(HDR % ALIGN_MIN == 0, "header must keep payloads aligned");
_Static_assert(HDR >= sizeof(struct aligned_meta),
               "allocation metadata probe must fit in the block header");
_Static_assert(ARENA_SIZE > HDR + ALIGN_MIN, "arena must hold one block");

static _Alignas(ALIGN_MIN) unsigned char arena[ARENA_SIZE];

static struct blk* free_list; /* first fit, newest at the head */
static int         arena_ready;

static uintptr_t                  arena_lock_owner;
static _Thread_local unsigned int arena_lock_depth;

#define ALIGNED_META_MAGIC ((size_t) 0x41535449414d454du)

static void arena_lock(void) {
    const uintptr_t owner = (uintptr_t) &arena_lock_depth;

    if (arena_lock_depth != 0) {
        ++arena_lock_depth;

        return;
    }

    for (;;) {
        uintptr_t expected = 0;

        if (__atomic_compare_exchange_n(&arena_lock_owner, &expected, owner, 0,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            arena_lock_depth = 1;

            return;
        }

        while (__atomic_load_n(&arena_lock_owner, __ATOMIC_RELAXED) != 0) {
            __atomic_signal_fence(__ATOMIC_ACQUIRE);
        }
    }
}

static void arena_unlock(void) {
    if (--arena_lock_depth != 0)
        return;

    __atomic_store_n(&arena_lock_owner, 0, __ATOMIC_RELEASE);
}

void __astia_malloc_fork_prepare(void) {
    arena_lock();
}

void __astia_malloc_fork_parent(void) {
    arena_unlock();
}

void __astia_malloc_fork_child(void) {
    arena_lock_depth = 0;

    __atomic_store_n(&arena_lock_owner, 0, __ATOMIC_RELEASE);
}

static int aligned_unwrap(void* ptr, void** base, size_t* alignment) {
    struct aligned_meta meta;

    memcpy(&meta, (char*) ptr - sizeof meta, sizeof meta);

    if (meta.magic != ALIGNED_META_MAGIC || meta.user != ptr || meta.base == NULL ||
        (uintptr_t) meta.base % ALIGN_MIN != 0 ||
        (uintptr_t) meta.base >= (uintptr_t) ptr)
        return 0;

    if (base)
        *base = meta.base;

    if (alignment)
        *alignment = meta.alignment;

    return 1;
}

static void arena_init(void) {
    struct blk* b = (struct blk*) (void*) arena;

    b->size     = ARENA_SIZE - HDR;
    b->free     = 1;
    b->prev     = NULL;
    b->next     = NULL;
    b->fd       = NULL;
    b->bk       = NULL;
    free_list   = b;
    arena_ready = 1;
}

static void list_unlink(struct blk* b) {
    if (b->bk)
        b->bk->fd = b->fd;
    else
        free_list = b->fd;

    if (b->fd)
        b->fd->bk = b->bk;

    b->fd = NULL;
    b->bk = NULL;
}

static void list_insert(struct blk* b) {
    b->free = 1;
    b->fd   = free_list;
    b->bk   = NULL;

    if (free_list)
        free_list->bk = b;

    free_list = b;
}

/* Carve `payload` off the front of b (already unlinked from the free
   list).  Returns the remainder block, or NULL if the tail would be too
   small to hold a header + minimum payload. */
static struct blk* split(struct blk* b, size_t payload) {
    struct blk* t;

    if (b->size < payload + HDR + ALIGN_MIN)
        return NULL;

    t       = (struct blk*) ((char*) b + HDR + payload);
    t->size = b->size - payload - HDR;
    t->free = 0;
    t->prev = b;
    t->next = b->next;

    if (b->next)
        b->next->prev = t;

    b->next = t;
    b->size = payload;

    return t;
}

/*
 * Return b to the free list and fold it into any free neighbours.
 *
 * Order matters and this is the subtle function of the file: neighbours
 * are merged while they are still the ones sitting in the list, and only
 * the *survivor* gets inserted.  (An earlier version unlinked the free
 * prev-neighbour and let it absorb b — then never put it back: a block
 * marked free with cleared links, whose later unlink mistakes it for the
 * list head and empties free_list.  The tests found that one.)
 */
static void release(struct blk* b) {
    b->free = 1;

    if (b->next && b->next->free) {
        struct blk* n = b->next;

        list_unlink(n); /* n is genuinely in the list */

        b->size += HDR + n->size;
        b->next = n->next;

        if (n->next)
            n->next->prev = b;
    }

    if (b->prev && b->prev->free) {
        struct blk* p = b->prev; /* p absorbs b; p *stays* listed */

        p->size += HDR + b->size;
        p->next = b->next;

        if (b->next)
            b->next->prev = p;

        b->free = 0; /* b's header is interior now */

        return;
    }

    list_insert(b); /* b survives on its own */
}

/* Allocate from the arena without touching errno: malloc() owns the POSIX
   ENOMEM contract, posix_memalign() must leave errno alone. */
static void* arena_alloc(size_t size) {
    struct blk* b;
    struct blk* t;

    size_t need;

    if (!arena_ready)
        arena_init();

    if (size > ARENA_SIZE) /* never fits; also rules out wrap-around */
        return NULL;

    need = ALIGN_UP(size);
    if (need == 0)
        need = ALIGN_MIN; /* malloc(0): a distinct minimum block */

    for (b = free_list; b != NULL; b = b->fd) {
        if (b->size >= need)
            break;
    }

    if (b == NULL)
        return NULL;

    list_unlink(b);

    b->free = 0;
    t       = split(b, need);
    if (t)
        release(t);

    return (char*) b + HDR;
}

static void arena_free(void* ptr) {
    void* base;

    if (aligned_unwrap(ptr, &base, NULL))
        ptr = base;

    release((struct blk*) ((char*) ptr - HDR));
}

static int arena_memalign(void** memptr, size_t alignment, size_t size) {
    size_t overhead;
    size_t request;

    void*                base;
    uintptr_t            candidate;
    uintptr_t            aligned;
    struct aligned_meta* meta;

    if (alignment - 1 > (size_t) -1 - sizeof(struct aligned_meta))
        return ENOMEM;

    overhead = sizeof(struct aligned_meta) + alignment - 1;

    if (size > (size_t) -1 - overhead)
        return ENOMEM;

    request = size + overhead;
    base    = arena_alloc(request);
    if (base == NULL)
        return ENOMEM;

    candidate = (uintptr_t) base + sizeof(struct aligned_meta);

    if (candidate < (uintptr_t) base || candidate > (uintptr_t) -1 - (alignment - 1)) {
        arena_free(base);

        return ENOMEM;
    }

    aligned         = (candidate + alignment - 1) & ~(uintptr_t) (alignment - 1);
    meta            = (struct aligned_meta*) (aligned - sizeof *meta);
    meta->magic     = ALIGNED_META_MAGIC;
    meta->base      = base;
    meta->user      = (void*) aligned;
    meta->alignment = alignment;
    *memptr         = meta->user;

    return 0;
}

static void* arena_realloc(void* ptr, size_t size) {
    struct blk* b;
    struct blk* t;

    size_t need;
    size_t alignment;

    void* base;

    if (aligned_unwrap(ptr, &base, &alignment)) {
        struct blk* bb = (struct blk*) ((char*) base - HDR);

        size_t offset = (size_t) ((char*) ptr - (char*) base);
        size_t usable = bb->size - offset;

        void* np;

        if (size <= usable)
            return ptr;

        if (arena_memalign(&np, alignment, size) != 0) {
            errno = ENOMEM;

            return NULL;
        }

        memcpy(np, ptr, usable);
        release(bb);

        return np;
    }

    b = (struct blk*) ((char*) ptr - HDR);

    if (size > ARENA_SIZE) {
        errno = ENOMEM;

        return NULL; /* cannot succeed; the old block survives */
    }

    need = ALIGN_UP(size);
    if (need == 0)
        need = ALIGN_MIN;

    if (b->size >= need) { /* shrink: split the tail back off */
        t = split(b, need);
        if (t)
            release(t);

        return ptr;
    }

    if (b->next != NULL && b->next->free &&
        b->size + HDR + b->next->size >= need) { /* grow in place */
        struct blk* n = b->next;

        list_unlink(n);

        b->size += HDR + n->size;
        b->next = n->next;

        if (n->next)
            n->next->prev = b;

        t = split(b, need);
        if (t)
            release(t);

        return ptr;
    }

    { /* relocate */
        void* np = arena_alloc(size);
        if (np == NULL) {
            errno = ENOMEM;

            return NULL;
        }

        memcpy(np, ptr, b->size < need ? b->size : need);
        arena_free(ptr);

        return np;
    }
}

void* malloc(size_t size) {
    void* ptr;

    arena_lock();

    ptr = arena_alloc(size);

    arena_unlock();

    if (ptr == NULL)
        errno = ENOMEM;

    return ptr;
}

void free(void* ptr) {
    if (ptr == NULL)
        return; /* free(NULL) is a no-op, per the standard */

    arena_lock();
    arena_free(ptr);
    arena_unlock();
}

int posix_memalign(void** memptr, size_t alignment, size_t size) {
    int result;

    if (memptr == NULL || alignment == 0 || (alignment & (alignment - 1)) != 0 ||
        alignment % sizeof(void*) != 0)
        return EINVAL;

    arena_lock();

    result = arena_memalign(memptr, alignment, size);

    arena_unlock();

    return result;
}

void* calloc(size_t nmemb, size_t size) {
    size_t bytes;

    void* p;

    if (nmemb != 0 && size > ((size_t) -1) / nmemb) {
        errno = ENOMEM;

        return NULL; /* nmemb * size would wrap */
    }

    bytes = nmemb * size;

    arena_lock();

    p = arena_alloc(bytes);
    if (p != NULL)
        memset(p, 0, bytes);

    arena_unlock();

    if (p == NULL) {
        errno = ENOMEM;

        return NULL;
    }

    return p;
}

void* realloc(void* ptr, size_t size) {
    void* result;

    if (ptr == NULL)
        return malloc(size);

    if (size == 0) {
        free(ptr); /* v1 semantics: free and return NULL */

        return NULL;
    }

    arena_lock();

    result = arena_realloc(ptr, size);

    arena_unlock();

    return result;
}
