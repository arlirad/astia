#include "../internal/socket_wire.h"
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
long __astia_fio_socket(int, int, int);
long __astia_fio_bind(int, const void*, uint32_t);
long __astia_fio_socketpair(int, int, int, int[2]);
long __astia_fio_getsockopt(int, int, int, void*, uint32_t, uint32_t*);
long __astia_fio_shutdown(int, int);
long __astia_fio_listen(int, int);
long __astia_fio_accept(int, void*, uint32_t*);
long __astia_fio_connect(int, const void*, uint32_t);
long __astia_fio_send(int, const void*, unsigned long, int);
long __astia_fio_recv(int, void*, unsigned long, int);
long __astia_fio_sendto(int, const void*, unsigned long, int, const void*, uint32_t);
long __astia_fio_recvfrom(int, void*, unsigned long, int, void*, uint32_t*);
long __astia_fio_sendmsg(int, const void*, unsigned long,
                         const struct __astia_socket_msghdr*);
long __astia_fio_recvmsg(int, void*, unsigned long, struct __astia_socket_msghdr*);

static int r(long v) {
    if (v < 0) {
        errno = (int) -v;

        return -1;
    }

    return (int) v;
}

typedef struct {
    uint32_t      len;
    uint32_t      family;
    unsigned char data[120];
} astia_sockaddr;

int socket(int d, int t, int p) {
    return r(__astia_fio_socket(d, t, p));
}

int socketpair(int d, int t, int p, int fds[2]) {
    return r(__astia_fio_socketpair(d, t, p, fds));
}

int shutdown(int f, int how) {
    return r(__astia_fio_shutdown(f, how));
}

int getsockopt(int fd, int level, int option, void* value, socklen_t* length) {
    uint32_t capacity;

    long rc;

    if (value == NULL || length == NULL) {
        errno = EFAULT;

        return -1;
    }

    capacity = *length;
    rc       = __astia_fio_getsockopt(fd, level, option, value, capacity, &capacity);
    if (rc < 0) {
        errno = (int) -rc;

        return -1;
    }

    *length = (socklen_t) capacity;

    return 0;
}

static int addr(const struct sockaddr* in, unsigned int n, astia_sockaddr* out) {
    if (!in || n < 3 || n > 110 || in->sa_family != AF_UNIX) {
        errno = EINVAL;

        return -1;
    }

    memset(out, 0, sizeof *out);

    out->family = AF_UNIX;
    out->len    = n;

    memcpy(out->data, ((const unsigned char*) in) + 2, n - 2);

    return 0;
}

int bind(int f, const struct sockaddr* a, unsigned int n) {
    astia_sockaddr x;

    if (addr(a, n, &x) < 0)
        return -1;

    return r(__astia_fio_bind(f, &x, sizeof x));
}

int listen(int f, int b) {
    return r(__astia_fio_listen(f, b));
}

int accept(int f, struct sockaddr* a, unsigned int* n) {
    return r(__astia_fio_accept(f, a, n));
}

int connect(int f, const struct sockaddr* a, unsigned int n) {
    astia_sockaddr x;

    if (addr(a, n, &x) < 0)
        return -1;

    return r(__astia_fio_connect(f, &x, sizeof x));
}

long send(int f, const void* b, unsigned long n, int flags) {
    long v = __astia_fio_send(f, b, n, flags);
    if (v < 0) {
        errno = (int) -v;

        return -1;
    }

    return v;
}

long recv(int f, void* b, unsigned long n, int flags) {
    long v = __astia_fio_recv(f, b, n, flags);
    if (v < 0) {
        errno = (int) -v;

        return -1;
    }

    return v;
}

long sendto(int f, const void* b, unsigned long n, int flags, const struct sockaddr* a,
            unsigned int alen) {
    astia_sockaddr x;

    if (addr(a, alen, &x) < 0)
        return -1;

    long v = __astia_fio_sendto(f, b, n, flags, &x, sizeof x);
    if (v < 0) {
        errno = (int) -v;

        return -1;
    }

    return v;
}

long recvfrom(int f, void* b, unsigned long n, int flags, struct sockaddr* a,
              unsigned int* alen) {
    astia_sockaddr x;
    uint32_t       cap = sizeof x;

    if (a != NULL) {
        if (alen == NULL) {
            errno = EFAULT;

            return -1;
        }

        if (*alen < 2) {
            errno = EINVAL;

            return -1;
        }
    }

    long v = __astia_fio_recvfrom(f, b, n, flags, &x, &cap);
    if (v < 0) {
        errno = (int) -v;

        return -1;
    }

    if (a && alen) {
        unsigned int path = x.len > 2 ? x.len - 2 : 0;

        if (path > sizeof(struct sockaddr_un) - 2)
            path = sizeof(struct sockaddr_un) - 2;

        if (path > *alen - 2)
            path = *alen - 2;

        a->sa_family = (unsigned short) x.family;

        if (path)
            memcpy((unsigned char*) a + 2, x.data, path);

        *alen = x.len < 2 ? x.len : 2 + path;
    }

    return v;
}

static int msg_data(const struct msghdr* m, void** out, size_t* out_total) {
    size_t total = 0;

    if (m->msg_iovlen != 0 && m->msg_iov == NULL) {
        errno = EFAULT;

        return -1;
    }

    for (size_t i = 0; i < m->msg_iovlen; ++i) {
        const struct iovec* iov = &m->msg_iov[i];

        if (iov->iov_len != 0 && iov->iov_base == NULL) {
            errno = EFAULT;

            return -1;
        }

        if (SIZE_MAX - total < iov->iov_len) {
            errno = EINVAL;

            return -1;
        }

        total += iov->iov_len;
    }

    if (total > UINT32_MAX) {
        errno = EINVAL;

        return -1;
    }

    *out       = NULL;
    *out_total = total;

    if (total == 0)
        return 0;

    *out = malloc(total);

    if (*out == NULL) {
        errno = ENOMEM;

        return -1;
    }

    return 0;
}

long sendmsg(int f, const struct msghdr* m, int flags) {
    void* data = NULL;

    size_t total = 0;

    if (m == NULL) {
        errno = EFAULT;

        return -1;
    }

    if (m->msg_controllen != 0 && m->msg_control == NULL) {
        errno = EFAULT;

        return -1;
    }

    if (m->msg_controllen > UINT32_MAX) {
        errno = EINVAL;

        return -1;
    }

    if (msg_data(m, &data, &total) < 0)
        return -1;

    size_t offset = 0;

    for (size_t i = 0; i < m->msg_iovlen && offset < total; ++i) {
        const struct iovec* iov = &m->msg_iov[i];

        memcpy((unsigned char*) data + offset, iov->iov_base, iov->iov_len);

        offset += iov->iov_len;
    }

    struct __astia_socket_msghdr wire = {m->msg_control, (uint32_t) m->msg_controllen,
                                         (uint32_t) flags, 0, 0};

    long rc = __astia_fio_sendmsg(f, data, total, &wire);

    free(data);

    if (rc < 0) {
        errno = (int) -rc;

        return -1;
    }

    return rc;
}

long recvmsg(int f, struct msghdr* m, int flags) {
    void* data = NULL;

    size_t total = 0;

    if (m == NULL) {
        errno = EFAULT;

        return -1;
    }

    if (m->msg_controllen != 0 && m->msg_control == NULL) {
        errno = EFAULT;

        return -1;
    }

    if (m->msg_controllen > UINT32_MAX) {
        errno = EINVAL;

        return -1;
    }

    if (msg_data(m, &data, &total) < 0)
        return -1;

    struct __astia_socket_msghdr wire = {m->msg_control, (uint32_t) m->msg_controllen,
                                         (uint32_t) flags, 0, 0};

    long rc = __astia_fio_recvmsg(f, data, total, &wire);

    if (rc < 0) {
        free(data);

        errno = (int) -rc;

        return -1;
    }

    size_t moved = 0;
    size_t left  = (size_t) rc;

    for (size_t i = 0; i < m->msg_iovlen && left != 0; ++i) {
        const size_t take = m->msg_iov[i].iov_len < left ? m->msg_iov[i].iov_len : left;

        memcpy(m->msg_iov[i].iov_base, (unsigned char*) data + moved, take);

        moved += take;
        left -= take;
    }

    free(data);

    m->msg_flags      = (int) wire.msg_flags;
    m->msg_controllen = wire.controllen;
    m->msg_namelen    = 0;

    return rc;
}
