#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef unsigned int   socklen_t;
typedef unsigned short sa_family_t;

struct iovec;

enum {
    AF_UNSPEC       = 0,
    AF_LOCAL        = 1,
    AF_INET         = 2,
    AF_AX25         = 3,
    AF_IPX          = 4,
    AF_APPLETALK    = 5,
    AF_NETROM       = 6,
    AF_BRIDGE       = 7,
    AF_ATMPVC       = 8,
    AF_X25          = 9,
    AF_INET6        = 10,
    AF_ROSE         = 11,
    AF_DECNET       = 12,
    AF_NETBEUI      = 13,
    AF_SECURITY     = 14,
    AF_KEY          = 15,
    AF_NETLINK      = 16,
    AF_PACKET       = 17,
    AF_ASH          = 18,
    AF_ECONET       = 19,
    AF_ATMSVC       = 20,
    AF_RDS          = 21,
    AF_SNA          = 22,
    AF_IRDA         = 23,
    AF_PPPOX        = 24,
    AF_WANPIPE      = 25,
    AF_LLC          = 26,
    AF_IB           = 27,
    AF_MPLS         = 28,
    AF_CAN          = 29,
    AF_TIPC         = 30,
    AF_BLUETOOTH    = 31,
    AF_IUCV         = 32,
    AF_RXRPC        = 33,
    AF_ISDN         = 34,
    AF_PHONET       = 35,
    AF_IEEE802154   = 36,
    AF_CAIF         = 37,
    AF_ALG          = 38,
    AF_NFC          = 39,
    AF_VSOCK        = 40,
    AF_KCM          = 41,
    AF_QIPCRTR      = 42,
    AF_SMC          = 43,
    AF_XDP          = 44,
    AF_MCTP         = 45,
    SOCK_STREAM     = 1,
    SOCK_DGRAM      = 2,
    SOCK_RAW        = 3,
    SOCK_SEQPACKET  = 5,
    SOL_SOCKET      = 1,
    SO_SNDBUF       = 7,
    SO_RCVBUF       = 8,
    SO_TYPE         = 3,
    SHUT_RD         = 0,
    SHUT_WR         = 1,
    SHUT_RDWR       = 2,
    SCM_RIGHTS      = 1,
    SCM_CREDENTIALS = 2
};

#define AF_UNIX AF_LOCAL

#define PF_UNSPEC AF_UNSPEC
#define PF_LOCAL AF_LOCAL
#define PF_UNIX AF_UNIX
#define PF_INET AF_INET
#define PF_INET6 AF_INET6
#define PF_NETLINK AF_NETLINK
#define PF_PACKET AF_PACKET

#define SOMAXCONN 128

#define SO_REUSEADDR 2
#define SO_ERROR 4
#define SO_DONTROUTE 5
#define SO_BROADCAST 6
#define SO_KEEPALIVE 9
#define SO_OOBINLINE 10
#define SO_LINGER 13
#define SO_REUSEPORT 15
#define SO_RCVTIMEO 20
#define SO_SNDTIMEO 21
#define SO_ACCEPTCONN 30

struct linger {
    int l_onoff;
    int l_linger;
};

#define MSG_OOB 0x01
#define MSG_PEEK 0x02
#define MSG_DONTROUTE 0x04
#define MSG_CTRUNC 0x08
#define MSG_TRUNC 0x20
#define MSG_DONTWAIT 0x40
#define MSG_EOR 0x80
#define MSG_WAITALL 0x100
#define MSG_NOSIGNAL 0x4000
#define MSG_CMSG_CLOEXEC 0x40000000

struct sockaddr {
    unsigned short sa_family;
    char           sa_data[14];
};

struct sockaddr_storage {
    _Alignas(8) sa_family_t ss_family;
    unsigned char __ss_padding[126];
};

struct cmsghdr {
    size_t cmsg_len;
    int    cmsg_level;
    int    cmsg_type;
};

struct msghdr {
    void*         msg_name;
    socklen_t     msg_namelen;
    struct iovec* msg_iov;
    size_t        msg_iovlen;
    void*         msg_control;
    size_t        msg_controllen;
    int           msg_flags;
};

#define CMSG_ALIGN(len) (((len) + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1))
#define CMSG_DATA(cmsg) ((unsigned char*) ((struct cmsghdr*) (cmsg) + 1))
#define CMSG_SPACE(len) (CMSG_ALIGN(sizeof(struct cmsghdr)) + CMSG_ALIGN(len))
#define CMSG_LEN(len) (CMSG_ALIGN(sizeof(struct cmsghdr)) + (len))
#define CMSG_FIRSTHDR(msg)                           \
    ((msg)->msg_controllen >= sizeof(struct cmsghdr) \
         ? (struct cmsghdr*) (msg)->msg_control      \
         : (struct cmsghdr*) 0)
#define CMSG_NXTHDR(msg, cmsg)                                                    \
    ((cmsg) == (struct cmsghdr*) 0                                                \
         ? CMSG_FIRSTHDR(msg)                                                     \
         : (((unsigned char*) (cmsg) + CMSG_ALIGN((cmsg)->cmsg_len) +             \
             CMSG_ALIGN(sizeof(struct cmsghdr))) >                                \
                    ((unsigned char*) (msg)->msg_control + (msg)->msg_controllen) \
                ? (struct cmsghdr*) 0                                             \
                : (struct cmsghdr*) ((unsigned char*) (cmsg) +                    \
                                     CMSG_ALIGN((cmsg)->cmsg_len))))

int  socket(int, int, int);
int  getsockopt(int, int, int, void*, socklen_t*);
int  setsockopt(int, int, int, const void*, socklen_t);
int  getsockname(int, struct sockaddr*, socklen_t*);
int  getpeername(int, struct sockaddr*, socklen_t*);
int  bind(int, const struct sockaddr*, socklen_t);
int  listen(int, int);
int  accept(int, struct sockaddr*, socklen_t*);
int  connect(int, const struct sockaddr*, socklen_t);
int  socketpair(int, int, int, int[2]);
int  shutdown(int, int);
long send(int, const void*, unsigned long, int);
long recv(int, void*, unsigned long, int);
long sendto(int, const void*, unsigned long, int, const struct sockaddr*, unsigned int);
long recvfrom(int, void*, unsigned long, int, struct sockaddr*, unsigned int*);
long sendmsg(int, const struct msghdr*, int);
long recvmsg(int, struct msghdr*, int);
#ifdef __cplusplus
}
#endif
