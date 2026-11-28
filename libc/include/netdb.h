#pragma once

#include <netinet/in.h>
#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

struct hostent {
    char*  h_name;
    char** h_aliases;
    int    h_addrtype;
    int    h_length;
    char** h_addr_list;
};

#define h_addr h_addr_list[0]

#define HOST_NOT_FOUND 1
#define TRY_AGAIN 2
#define NO_RECOVERY 3
#define NO_DATA 4
#define NO_ADDRESS NO_DATA

extern int h_errno;

struct addrinfo {
    int              ai_flags;
    int              ai_family;
    int              ai_socktype;
    int              ai_protocol;
    socklen_t        ai_addrlen;
    struct sockaddr* ai_addr;
    char*            ai_canonname;
    struct addrinfo* ai_next;
};

#define AI_PASSIVE 0x0001
#define AI_CANONNAME 0x0002
#define AI_NUMERICHOST 0x0004
#define AI_V4MAPPED 0x0008
#define AI_ALL 0x0010
#define AI_ADDRCONFIG 0x0020
#define AI_NUMERICSERV 0x0400

#define NI_NOFQDN 0x0004
#define NI_NUMERICHOST 0x0001
#define NI_NAMEREQD 0x0008
#define NI_NUMERICSERV 0x0002
#define NI_DGRAM 0x0010

#define NI_MAXHOST 1025
#define NI_MAXSERV 32

#define EAI_BADFLAGS (-1)
#define EAI_NONAME (-2)
#define EAI_AGAIN (-3)
#define EAI_FAIL (-4)
#define EAI_FAMILY (-6)
#define EAI_SOCKTYPE (-7)
#define EAI_SERVICE (-8)
#define EAI_MEMORY (-10)
#define EAI_SYSTEM (-11)
#define EAI_OVERFLOW (-12)

int  getaddrinfo(const char* node, const char* service, const struct addrinfo* hints,
                 struct addrinfo** result);
void freeaddrinfo(struct addrinfo* result);
const char*     gai_strerror(int error);
struct hostent* gethostbyname(const char* name);

int getnameinfo(const struct sockaddr* address, socklen_t address_length, char* host,
                socklen_t host_length, char* service, socklen_t service_length,
                int flags);

#ifdef __cplusplus
}
#endif
