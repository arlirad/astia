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
    AF_UNIX         = 1,
    AF_INET         = 2,
    AF_INET6        = 10,
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
