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
 * Threading: the arena and both lists are shared mutable state, so this
 * is NOT thread-safe yet — there is no lock primitive to use (astia has
 * no futex yet).  The plan is one arena mutex with an uncontended
 * cmpxchg fast path the moment astia_futex exists, and the structure is
 * kept ready for it: all mutation happens inside exactly three entry
 * points (malloc/free/realloc), none of them runs user code or blocks
 * while holding state, so wrapping them is a pure mechanical change with
 * no locking order to reason about.  Until then: one allocator user per
 * process, like every libc before it got locks.
 */

#define ARENA_SIZE (1u << 20)   /* 1 MiB of BSS: costs no file space */
#define ALIGN_MIN  16u
#define ALIGN_UP(x) \
    (((x) + (size_t) (ALIGN_MIN - 1)) & ~(size_t) (ALIGN_MIN - 1))

struct blk {
    size_t size;        /* payload bytes, a multiple of ALIGN_MIN */
    int    free;        /* on the free list? */
    struct blk* prev;   /* address-order neighbours ... */
    struct blk* next;
    struct blk* fd;     /* ... and free-list links (free blocks only) */
    struct blk* bk;
};

#define HDR sizeof(struct blk)

_Static_assert(HDR % ALIGN_MIN == 0, "header must keep payloads aligned");
_Static_assert(ARENA_SIZE > HDR + ALIGN_MIN, "arena must hold one block");

static _Alignas(ALIGN_MIN) unsigned char arena[ARENA_SIZE];

static struct blk* free_list;   /* first fit, newest at the head */
static int arena_ready;

static void arena_init(void)
{
    struct blk* b = (struct blk*) (void*) arena;

    b->size = ARENA_SIZE - HDR;
    b->free = 1;
    b->prev = NULL;
    b->next = NULL;
    b->fd = NULL;
    b->bk = NULL;
    free_list = b;
    arena_ready = 1;
}

static void list_unlink(struct blk* b)
{
    if (b->bk)
        b->bk->fd = b->fd;
    else
        free_list = b->fd;
    if (b->fd)
        b->fd->bk = b->bk;
    b->fd = NULL;
    b->bk = NULL;
}

static void list_insert(struct blk* b)
{
    b->free = 1;
    b->fd = free_list;
    b->bk = NULL;
    if (free_list)
        free_list->bk = b;
    free_list = b;
}

/* Carve `payload` off the front of b (already unlinked from the free
   list).  Returns the remainder block, or NULL if the tail would be too
   small to hold a header + minimum payload. */
static struct blk* split(struct blk* b, size_t payload)
{
    struct blk* t;

    if (b->size < payload + HDR + ALIGN_MIN)
        return NULL;
    t = (struct blk*) ((char*) b + HDR + payload);
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
static void release(struct blk* b)
{
    b->free = 1;

    if (b->next && b->next->free) {
        struct blk* n = b->next;
        list_unlink(n);                 /* n is genuinely in the list */
        b->size += HDR + n->size;
        b->next = n->next;
        if (n->next)
            n->next->prev = b;
    }
    if (b->prev && b->prev->free) {
        struct blk* p = b->prev;        /* p absorbs b; p *stays* listed */
        p->size += HDR + b->size;
        p->next = b->next;
        if (b->next)
            b->next->prev = p;
        b->free = 0;                    /* b's header is interior now */
        return;
    }
    list_insert(b);                     /* b survives on its own */
}

void* malloc(size_t size)
{
    struct blk* b;
    struct blk* t;
    size_t need;

    if (!arena_ready)
        arena_init();

    if (size > ARENA_SIZE)     /* never fits; also rules out wrap-around */
        return NULL;
    need = ALIGN_UP(size);
    if (need == 0)
        need = ALIGN_MIN;      /* malloc(0): a distinct minimum block */

    for (b = free_list; b != NULL; b = b->fd)
        if (b->size >= need)
            break;
    if (b == NULL)
        return NULL;

    list_unlink(b);
    b->free = 0;
    t = split(b, need);
    if (t)
        release(t);
    return (char*) b + HDR;
}

void free(void* ptr)
{
    if (ptr == NULL)
        return;                /* free(NULL) is a no-op, per the standard */
    release((struct blk*) ((char*) ptr - HDR));
}

void* calloc(size_t nmemb, size_t size)
{
    size_t bytes;
    void* p;

    if (nmemb != 0 && size > ((size_t) -1) / nmemb)
        return NULL;           /* nmemb * size would wrap */
    bytes = nmemb * size;
    p = malloc(bytes);
    if (p)
        memset(p, 0, bytes);
    return p;
}

void* realloc(void* ptr, size_t size)
{
    struct blk* b;
    struct blk* t;
    size_t need;

    if (ptr == NULL)
        return malloc(size);
    if (size == 0) {
        free(ptr);             /* v1 semantics: free and return NULL */
        return NULL;
    }

    b = (struct blk*) ((char*) ptr - HDR);
    if (size > ARENA_SIZE)
        return NULL;           /* cannot succeed; the old block survives */
    need = ALIGN_UP(size);
    if (need == 0)
        need = ALIGN_MIN;

    if (b->size >= need) {     /* shrink: split the tail back off */
        t = split(b, need);
        if (t)
            release(t);
        return ptr;
    }

    if (b->next != NULL && b->next->free &&
        b->size + HDR + b->next->size >= need) {   /* grow in place */
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

    {                                   /* relocate */
        void* np = malloc(size);
        if (np == NULL)
            return NULL;
        memcpy(np, ptr, b->size < need ? b->size : need);
        free(ptr);
        return np;
    }
}
