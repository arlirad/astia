#pragma once
#ifdef __cplusplus
extern "C" {
#endif
typedef unsigned int   socklen_t;
typedef unsigned short sa_family_t;

enum {
    AF_UNSPEC      = 0,
    AF_UNIX        = 1,
    AF_INET        = 2,
    AF_INET6       = 10,
    SOCK_STREAM    = 1,
    SOCK_DGRAM     = 2,
    SOCK_RAW       = 3,
    SOCK_SEQPACKET = 5,
    SOL_SOCKET     = 1,
    SO_SNDBUF      = 7,
    SO_RCVBUF      = 8,
    SO_TYPE        = 3,
    SHUT_RD        = 0,
    SHUT_WR        = 1,
    SHUT_RDWR      = 2
};

struct sockaddr {
    unsigned short sa_family;
    char           sa_data[14];
};

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
#ifdef __cplusplus
}
#endif
