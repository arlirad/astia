#include "../../src/internal/socket_wire.h"
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/uio.h>

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

long __astia_fio_sendmsg(int fd, const void* buffer, unsigned long length,
                         const struct __astia_socket_msghdr* header) {
    struct iovec  iov     = {(void*) buffer, length};
    struct msghdr message = {0};

    message.msg_iov        = &iov;
    message.msg_iovlen     = 1;
    message.msg_control    = header->control;
    message.msg_controllen = header->controllen;

    long rc = syscall(SYS_sendmsg, fd, &message, header->flags);

    return rc < 0 ? -HOST_ERRNO : rc;
}

long __astia_fio_recvmsg(int fd, void* buffer, unsigned long length,
                         struct __astia_socket_msghdr* header) {
    struct iovec  iov     = {buffer, length};
    struct msghdr message = {0};

    message.msg_iov        = &iov;
    message.msg_iovlen     = 1;
    message.msg_control    = header->control;
    message.msg_controllen = header->controllen;

    long rc = syscall(SYS_recvmsg, fd, &message, header->flags);

    if (rc < 0)
        return -HOST_ERRNO;

    header->msg_flags  = (uint32_t) message.msg_flags;
    header->controllen = (uint32_t) message.msg_controllen;

    return rc;
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

typedef struct {
    uint32_t      length;
    uint32_t      family;
    unsigned char data[120];
} astia_sockaddr_shim;

static long host_sockaddr(const void* address, unsigned char* raw, unsigned int* length) {
    const astia_sockaddr_shim* in = address;

    if (!in || in->family != AF_UNIX || in->length < 3 || in->length > 2 + 108)
        return -EINVAL;

    raw[0] = (unsigned char) (in->family & 0xff);
    raw[1] = (unsigned char) ((in->family >> 8) & 0xff);

    __builtin_memcpy(raw + 2, in->data, in->length - 2);

    *length = in->length;

    return 0;
}

long __astia_fio_bind(int fd, const void* address, uint32_t length) {
    unsigned char raw[2 + 108];
    unsigned int  n;

    (void) length;

    const long converted = host_sockaddr(address, raw, &n);
    if (converted < 0)
        return converted;

    long rc = syscall(SYS_bind, fd, raw, n);

    return rc < 0 ? -HOST_ERRNO : rc;
}

long __astia_fio_connect(int fd, const void* address, uint32_t length) {
    unsigned char raw[2 + 108];
    unsigned int  n;

    (void) length;

    const long converted = host_sockaddr(address, raw, &n);
    if (converted < 0)
        return converted;

    long rc = syscall(SYS_connect, fd, raw, n);

    return rc < 0 ? -HOST_ERRNO : rc;
}

long __astia_fio_listen(int fd, int backlog) {
    long rc = syscall(SYS_listen, fd, backlog);

    return rc < 0 ? -HOST_ERRNO : rc;
}

long __astia_fio_accept(int fd, void* address, uint32_t* length) {
    unsigned char raw[2 + 108];
    unsigned int  raw_length = sizeof raw;

    astia_sockaddr_shim* out = address;

    if (out == NULL) {
        long rc = syscall(SYS_accept, fd, 0, 0);

        return rc < 0 ? -HOST_ERRNO : rc;
    }

    if (length == NULL)
        return -EFAULT;

    long rc = syscall(SYS_accept, fd, raw, &raw_length);
    if (rc < 0)
        return -HOST_ERRNO;

    __builtin_memset(out, 0, sizeof *out);

    out->length = raw_length;

    if (raw_length >= 2) {
        out->family = (uint32_t) raw[0] | ((uint32_t) raw[1] << 8);

        if (raw_length > 2)
            __builtin_memcpy(out->data, raw + 2, raw_length - 2);
    }

    *length = raw_length;

    return rc;
}

long __astia_fio_recvfrom(int fd, void* buffer, unsigned long length, int flags,
                          void* address, uint32_t* address_length) {
    unsigned char raw[2 + 108];
    unsigned int  raw_length = sizeof raw;

    astia_sockaddr_shim* out = address;

    (void) address_length;

    long rc = syscall(SYS_recvfrom, fd, buffer, length, flags, raw, &raw_length);
    if (rc < 0)
        return -HOST_ERRNO;

    __builtin_memset(out, 0, sizeof *out);

    out->length = raw_length;

    if (raw_length >= 2) {
        out->family = (uint32_t) raw[0] | ((uint32_t) raw[1] << 8);

        if (raw_length > 2)
            __builtin_memcpy(out->data, raw + 2, raw_length - 2);
    }

    return rc;
}

UNSUPPORTED(__astia_fio_shutdown, (int fd, int how))
UNSUPPORTED(__astia_fio_sendto, (int fd, const void* buffer, unsigned long length,
                                 int flags, const void* address, uint32_t address_length))
