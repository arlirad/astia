#pragma once

#include <sys/socket.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t in_port_t;
typedef uint32_t in_addr_t;

struct in_addr {
    in_addr_t s_addr;
};

struct in6_addr {
    union {
        uint8_t  u6_addr8[16];
        uint16_t u6_addr16[8];
        uint32_t u6_addr32[4];
    } u6_addr;
};

#define s6_addr u6_addr.u6_addr8
#define s6_addr16 u6_addr.u6_addr16
#define s6_addr32 u6_addr.u6_addr32

struct sockaddr_in {
    sa_family_t    sin_family;
    in_port_t      sin_port;
    struct in_addr sin_addr;
    unsigned char  sin_zero[8];
};

struct sockaddr_in6 {
    sa_family_t     sin6_family;
    in_port_t       sin6_port;
    uint32_t        sin6_flowinfo;
    struct in6_addr sin6_addr;
    uint32_t        sin6_scope_id;
};

#define INET_ADDRSTRLEN 16
#define INET6_ADDRSTRLEN 46

#define IPPROTO_IP 0
#define IPPROTO_ICMP 1
#define IPPROTO_IGMP 2
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define IPPROTO_IPV6 41
#define IPPROTO_RAW 255

#define INADDR_ANY ((in_addr_t) 0x00000000u)
#define INADDR_BROADCAST ((in_addr_t) 0xffffffffu)
#define INADDR_LOOPBACK ((in_addr_t) 0x7f000001u)
#define INADDR_NONE ((in_addr_t) 0xffffffffu)

#define IN_CLASSA_NET 0xff000000u
#define IN_CLASSA_NSHIFT 24
#define IN_CLASSA_HOST (0xffffffffu & ~IN_CLASSA_NET)
#define IN_CLASSA_MAX 128
#define IN_CLASSB_NET 0xffff0000u
#define IN_CLASSB_NSHIFT 16
#define IN_CLASSB_HOST (0xffffffffu & ~IN_CLASSB_NET)
#define IN_CLASSB_MAX 65536
#define IN_CLASSC_NET 0xffffff00u
#define IN_CLASSC_NSHIFT 8
#define IN_CLASSC_HOST (0xffffffffu & ~IN_CLASSC_NET)
#define IN_CLASSD_NET 0xf0000000u
#define IN_CLASSD_NSHIFT 28
#define IN_CLASSD_HOST (0xffffffffu & ~IN_CLASSD_NET)

#define IN_CLASSA(a) ((((in_addr_t) (a)) & 0x80000000u) == 0)
#define IN_CLASSB(a) ((((in_addr_t) (a)) & 0xc0000000u) == 0x80000000u)
#define IN_CLASSC(a) ((((in_addr_t) (a)) & 0xe0000000u) == 0xc0000000u)
#define IN_CLASSD(a) ((((in_addr_t) (a)) & 0xf0000000u) == 0xe0000000u)
#define IN_MULTICAST(a) IN_CLASSD(a)
#define IN_EXPERIMENTAL(a) ((((in_addr_t) (a)) & 0xf0000000u) == 0xf0000000u)
#define IN_LOOPBACK(a) ((((in_addr_t) (a)) & IN_CLASSA_NET) == 0x7f000000u)
#define IN_LOOPBACKNET 127

#define IN6ADDR_ANY_INIT                                       \
    {                                                          \
        {                                                      \
            { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } \
        }                                                      \
    }
#define IN6ADDR_LOOPBACK_INIT                                  \
    {                                                          \
        {                                                      \
            { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 } \
        }                                                      \
    }

static inline int __astia_in6_unspecified(const struct in6_addr* address) {
    for (int i = 0; i < 16; ++i) {
        if (address->s6_addr[i] != 0)
            return 0;
    }

    return 1;
}

static inline int __astia_in6_loopback(const struct in6_addr* address) {
    for (int i = 0; i < 15; ++i) {
        if (address->s6_addr[i] != 0)
            return 0;
    }

    return address->s6_addr[15] == 1;
}

static inline int __astia_in6_v4mapped(const struct in6_addr* address) {
    for (int i = 0; i < 10; ++i) {
        if (address->s6_addr[i] != 0)
            return 0;
    }

    return address->s6_addr[10] == 0xff && address->s6_addr[11] == 0xff;
}

static inline int __astia_in6_v4compat(const struct in6_addr* address) {
    for (int i = 0; i < 12; ++i) {
        if (address->s6_addr[i] != 0)
            return 0;
    }

    const uint32_t tail = ((uint32_t) address->s6_addr[12] << 24) |
                          ((uint32_t) address->s6_addr[13] << 16) |
                          ((uint32_t) address->s6_addr[14] << 8) | address->s6_addr[15];

    return tail != 0 && tail != 1;
}

static inline int __astia_in6_linklocal(const struct in6_addr* address) {
    return address->s6_addr[0] == 0xfe && (address->s6_addr[1] & 0xc0) == 0x80;
}

static inline int __astia_in6_sitelocal(const struct in6_addr* address) {
    return address->s6_addr[0] == 0xfe && (address->s6_addr[1] & 0xc0) == 0xc0;
}

#define IN6_IS_ADDR_UNSPECIFIED(a) __astia_in6_unspecified(a)
#define IN6_IS_ADDR_LOOPBACK(a) __astia_in6_loopback(a)
#define IN6_IS_ADDR_V4MAPPED(a) __astia_in6_v4mapped(a)
#define IN6_IS_ADDR_V4COMPAT(a) __astia_in6_v4compat(a)
#define IN6_IS_ADDR_LINKLOCAL(a) __astia_in6_linklocal(a)
#define IN6_IS_ADDR_SITELOCAL(a) __astia_in6_sitelocal(a)
#define IN6_IS_ADDR_MULTICAST(a) ((a)->s6_addr[0] == 0xff)
#define IN6_IS_ADDR_MC_LINKLOCAL(a) \
    (IN6_IS_ADDR_MULTICAST(a) && ((a)->s6_addr[1] & 0x0f) == 0x02)
#define IN6_IS_ADDR_MC_NODELOCAL(a) \
    (IN6_IS_ADDR_MULTICAST(a) && ((a)->s6_addr[1] & 0x0f) == 0x01)

static inline uint16_t __astia_bswap16(uint16_t value) {
    return (uint16_t) ((value << 8) | (value >> 8));
}

static inline uint32_t __astia_bswap32(uint32_t value) {
    return ((value & 0xff000000u) >> 24) | ((value & 0x00ff0000u) >> 8) |
           ((value & 0x0000ff00u) << 8) | ((value & 0x000000ffu) << 24);
}

#if !defined(__BYTE_ORDER__) || !defined(__ORDER_BIG_ENDIAN__) || \
    !defined(__ORDER_LITTLE_ENDIAN__)
#error "netinet/in.h requires the compiler's __BYTE_ORDER__ macros"
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
static inline uint16_t htons(uint16_t value) {
    return value;
}

static inline uint16_t ntohs(uint16_t value) {
    return value;
}

static inline uint32_t htonl(uint32_t value) {
    return value;
}

static inline uint32_t ntohl(uint32_t value) {
    return value;
}
#elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
static inline uint16_t htons(uint16_t value) {
    return __astia_bswap16(value);
}

static inline uint16_t ntohs(uint16_t value) {
    return __astia_bswap16(value);
}

static inline uint32_t htonl(uint32_t value) {
    return __astia_bswap32(value);
}

static inline uint32_t ntohl(uint32_t value) {
    return __astia_bswap32(value);
}
#else
#error "netinet/in.h: unsupported byte order"
#endif

#ifdef __cplusplus
}
#endif
