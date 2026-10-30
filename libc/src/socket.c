#include <sys/socket.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>
long __astia_fio_socket(int, int, int);
long __astia_fio_bind(int, const void*, uint32_t);
long __astia_fio_socketpair(int, int, int, int[2]);
long __astia_fio_shutdown(int, int);
long __astia_fio_listen(int, int);
long __astia_fio_accept(int, void*, uint32_t*);
long __astia_fio_connect(int, const void*, uint32_t);
long __astia_fio_send(int, const void*, unsigned long, int);
long __astia_fio_recv(int, void*, unsigned long, int);
long __astia_fio_sendto(int, const void*, unsigned long, int, const void*, uint32_t);
long __astia_fio_recvfrom(int, void*, unsigned long, int, void*, uint32_t*);
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
    long           v   = __astia_fio_recvfrom(f, b, n, flags, &x, &cap);
    if (v < 0) {
        errno = (int) -v;
        return -1;
    }
    if (a && alen) {
        unsigned int out = *alen;
        if (out < 3 || out > sizeof(struct sockaddr)) {
            errno = EINVAL;
            return -1;
        }
        a->sa_family = (unsigned short) x.family;
        if (x.len > 2)
            memcpy(a->sa_data, x.data, x.len - 2 > 12 ? 12 : x.len - 2);
        *alen = x.len;
    }
    return v;
}
