/*
 * mmap(2)/munmap(2) over the astia direct syscalls.  The values
 * mirror the kernel ABI in <astia/api/syscalls.h> (Linux numbering).
 */
#ifndef ASTIA_LIBC_SYS_MMAN_H
#define ASTIA_LIBC_SYS_MMAN_H

#include <stddef.h>

/* Protection bits (ABI word). */
#define PROT_NONE 0x0
#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_EXEC 0x4

/* Mapping flags (ABI word). */
#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02
#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20
#define MAP_STACK 0x20000

#define MAP_FAILED ((void*) -1)

#ifdef __cplusplus
extern "C" {
#endif

void* mmap(void* addr, size_t len, int prot, int flags, int fd, long off);
int   munmap(void* addr, size_t len);
int   mprotect(void* addr, size_t len, int prot);

#ifdef __cplusplus
}
#endif

#endif /* ASTIA_LIBC_SYS_MMAN_H */
