/*
 * The memory-management half of the process API.  The kernel returns
 * a base address or a negated errno; normalize to the libc
 * convention (MAP_FAILED + errno / -1 + errno), like process.c does
 * for the process calls.
 */
#include <errno.h>
#include <stdint.h>

/* The ABI header first: it *declares* the PROT_/MAP_ names as enum
   constants, and <sys/mman.h> then covers them with the
   same-valued macros. */
#include <astia/api/syscalls.h>
#include <sys/mman.h>

void* mmap(void* addr, size_t len, int prot, int flags, int fd, long off) {
    long rc = astia_mmap(addr, (uint64_t) len, (uint32_t) prot, (uint32_t) flags,
                         (int32_t) fd, (int64_t) off);
    if (rc < 0) {
        errno = (int) -rc;
        return MAP_FAILED;
    }
    return (void*) rc;
}

int munmap(void* addr, size_t len) {
    long rc = astia_munmap(addr, (uint64_t) len);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return (int) rc;
}

int mprotect(void* addr, size_t len, int prot) {
    long rc = astia_mprotect(addr, (uint64_t) len, (uint32_t) prot);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return (int) rc;
}

int msync(void* addr, size_t len, int flags) {
    long rc = astia_msync(addr, (uint64_t) len, (uint32_t) flags);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return (int) rc;
}

int shm_open(const char* name, int oflag, mode_t mode) {
    long rc = astia_shm_open(name, oflag, (uint32_t) mode);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return (int) rc;
}

int shm_unlink(const char* name) {
    long rc = astia_shm_unlink(name);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return 0;
}
