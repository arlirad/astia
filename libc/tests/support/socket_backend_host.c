#include <sys/syscall.h>

#include <errno.h>
#include <stdint.h>

extern long syscall(long number, ...);
extern int* __errno_location(void);

#define HOST_ERRNO (*__errno_location())

long __astia_fio_socket(int domain, int type, int protocol) {
    long rc = syscall(SYS_socket, domain, type, protocol);

    return rc < 0 ? -HOST_ERRNO : rc;
}

long __astia_fio_socketpair(int domain, int type, int protocol, int fds[2]) {
    long rc = syscall(SYS_socketpair, domain, type, protocol, fds);

    return rc < 0 ? -HOST_ERRNO : rc;
}

long __astia_fio_send(int fd, const void* buffer, unsigned long length, int flags) {
    long rc = syscall(SYS_sendto, fd, buffer, length, flags, 0, 0);

    return rc < 0 ? -HOST_ERRNO : rc;
}

long __astia_fio_recv(int fd, void* buffer, unsigned long length, int flags) {
    long rc = syscall(SYS_recvfrom, fd, buffer, length, flags, 0, 0);

    return rc < 0 ? -HOST_ERRNO : rc;
}

long __astia_fio_getsockopt(int fd, int level, int option, void* value, uint32_t capacity,
                            uint32_t* length) {
    unsigned int host_length = capacity;
    long         rc = syscall(SYS_getsockopt, fd, level, option, value, &host_length);
    if (rc < 0)
        return -HOST_ERRNO;

    *length = host_length;

    return 0;
}

#define UNSUPPORTED(name, signature) \
    long name signature {            \
        return -ENOSYS;              \
    }

UNSUPPORTED(__astia_fio_shutdown, (int fd, int how))
UNSUPPORTED(__astia_fio_bind, (int fd, const void* address, uint32_t length))
UNSUPPORTED(__astia_fio_listen, (int fd, int backlog))
UNSUPPORTED(__astia_fio_accept, (int fd, void* address, uint32_t* length))
UNSUPPORTED(__astia_fio_connect, (int fd, const void* address, uint32_t length))
UNSUPPORTED(__astia_fio_sendto, (int fd, const void* buffer, unsigned long length,
                                 int flags, const void* address, uint32_t address_length))
UNSUPPORTED(__astia_fio_recvfrom, (int fd, void* buffer, unsigned long length, int flags,
                                   void* address, uint32_t* address_length))
