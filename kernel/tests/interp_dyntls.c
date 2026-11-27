/* Global-dynamic TLS payload: the DSO's __tls_get_addr calls must reach the
 * interpreter's DTV, and each thread must get its own block. */
#include <astia/api/syscalls.h>

extern int      dyn_tls_value(void);
extern void     dyn_tls_set(int value);
extern uint64_t ld_dtv_entries(void) __attribute__((weak));

static void thread_entry(void* argument) {
    (void) argument;

    dyn_tls_set(99);

    if (dyn_tls_value() != 99)
        astia_thread_exit((void*) (uintptr_t) 2);

    astia_thread_exit(0);
}

void _start(void) {
    if (dyn_tls_value() != 77)
        astia_exit(51);

    dyn_tls_set(88);

    if (dyn_tls_value() != 88)
        astia_exit(52);

    const uint64_t stack_bytes = 64 * 1024;

    const long stack = astia_mmap(0, stack_bytes, PROT_READ | PROT_WRITE,
                                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (stack <= 0)
        astia_exit(56);

    const long tid = astia_thread_create(
        thread_entry, (void*) (uintptr_t) (stack + (long) stack_bytes), 0);
    if (tid <= 0)
        astia_exit(56);

    void* result = (void*) (uintptr_t) 1;

    if (astia_thread_join((uint32_t) tid, &result) != 0)
        astia_exit(57);

    if (result != 0)
        astia_exit(53);

    if (dyn_tls_value() != 88)
        astia_exit(54);

    if (ld_dtv_entries == 0 || ld_dtv_entries() != 2)
        astia_exit(55);

    astia_exit(0);

    for (;;) {
        __asm__ volatile("hlt");
    }
}
