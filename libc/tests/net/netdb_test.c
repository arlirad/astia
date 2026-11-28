#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <netdb.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition)                                                           \
    do {                                                                           \
        if (!(condition)) {                                                        \
            fprintf(stderr, "netdb check failed: %s:%d: %s\n", __FILE__, __LINE__, \
                    #condition);                                                   \
            ++failures;                                                            \
        }                                                                          \
    } while (0)

static void check_ipv4(void) {
    struct addrinfo  hints;
    struct addrinfo* result = NULL;

    memset(&hints, 0, sizeof hints);

    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    CHECK(getaddrinfo("192.0.2.1", "http", &hints, &result) == 0);
    CHECK(result != NULL);
    CHECK(result->ai_family == AF_INET);
    CHECK(result->ai_socktype == SOCK_STREAM);
    CHECK(result->ai_protocol == IPPROTO_TCP);
    CHECK(result->ai_addrlen == sizeof(struct sockaddr_in));
    CHECK(result->ai_next == NULL);

    if (result != NULL && result->ai_addr != NULL) {
        const struct sockaddr_in* address = (const struct sockaddr_in*) result->ai_addr;

        CHECK(address->sin_family == AF_INET);
        CHECK(ntohs(address->sin_port) == 80);
        CHECK(ntohl(address->sin_addr.s_addr) == 0xc0000201u);
    }

    freeaddrinfo(result);

    memset(&hints, 0, sizeof hints);

    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_CANONNAME;

    CHECK(getaddrinfo("192.0.2.1", "8080", &hints, &result) == 0);
    CHECK(result != NULL);
    CHECK(result->ai_canonname != NULL);

    if (result != NULL && result->ai_canonname != NULL)
        CHECK(strcmp(result->ai_canonname, "192.0.2.1") == 0);

    freeaddrinfo(result);

    memset(&hints, 0, sizeof hints);

    hints.ai_family = AF_INET;
    hints.ai_flags  = AI_PASSIVE;

    CHECK(getaddrinfo(NULL, "80", &hints, &result) == 0);
    CHECK(result != NULL);

    if (result != NULL && result->ai_addr != NULL) {
        const struct sockaddr_in* address = (const struct sockaddr_in*) result->ai_addr;

        CHECK(ntohl(address->sin_addr.s_addr) == INADDR_ANY);
    }

    freeaddrinfo(result);

    memset(&hints, 0, sizeof hints);

    hints.ai_family = AF_INET;

    CHECK(getaddrinfo(NULL, "80", &hints, &result) == 0);
    CHECK(result != NULL);

    if (result != NULL && result->ai_addr != NULL) {
        const struct sockaddr_in* address = (const struct sockaddr_in*) result->ai_addr;

        CHECK(ntohl(address->sin_addr.s_addr) == INADDR_LOOPBACK);
    }

    freeaddrinfo(result);
}

static void check_ipv6(void) {
    struct addrinfo  hints;
    struct addrinfo* result = NULL;

    memset(&hints, 0, sizeof hints);

    hints.ai_family   = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;

    CHECK(getaddrinfo("::1", "443", &hints, &result) == 0);
    CHECK(result != NULL);
    CHECK(result->ai_family == AF_INET6);
    CHECK(result->ai_addrlen == sizeof(struct sockaddr_in6));

    if (result != NULL && result->ai_addr != NULL) {
        const struct sockaddr_in6* address = (const struct sockaddr_in6*) result->ai_addr;

        CHECK(address->sin6_family == AF_INET6);
        CHECK(ntohs(address->sin6_port) == 443);
        CHECK(IN6_IS_ADDR_LOOPBACK(&address->sin6_addr));
    }

    freeaddrinfo(result);

    memset(&hints, 0, sizeof hints);

    hints.ai_family   = AF_INET6;
    hints.ai_socktype = SOCK_DGRAM;

    CHECK(getaddrinfo("2001:db8::1", "53", &hints, &result) == 0);
    CHECK(result != NULL);
    CHECK(result->ai_protocol == IPPROTO_UDP);

    freeaddrinfo(result);
}

static void check_defaults_and_errors(void) {
    struct addrinfo  hints;
    struct addrinfo* result = NULL;

    memset(&hints, 0, sizeof hints);

    CHECK(getaddrinfo("192.0.2.1", "80", &hints, &result) == 0);
    CHECK(result != NULL);

    if (result != NULL) {
        CHECK(result->ai_socktype == SOCK_STREAM);
        CHECK(result->ai_next != NULL);

        if (result->ai_next != NULL)
            CHECK(result->ai_next->ai_socktype == SOCK_DGRAM);
    }

    freeaddrinfo(result);

    memset(&hints, 0, sizeof hints);

    hints.ai_family = AF_INET;
    hints.ai_flags  = AI_NUMERICHOST;

    CHECK(getaddrinfo("example.com", "80", &hints, &result) == EAI_NONAME);

    memset(&hints, 0, sizeof hints);

    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    CHECK(getaddrinfo("192.0.2.1", "nosuchservice", &hints, &result) == EAI_SERVICE);
    CHECK(getaddrinfo("192.0.2.1", "99999", &hints, &result) == EAI_SERVICE);

    memset(&hints, 0, sizeof hints);

    hints.ai_family   = AF_INET;
    hints.ai_socktype = 999;

    CHECK(getaddrinfo("192.0.2.1", "80", &hints, &result) == EAI_SOCKTYPE);

    memset(&hints, 0, sizeof hints);

    hints.ai_family = 999;

    CHECK(getaddrinfo("192.0.2.1", "80", &hints, &result) == EAI_FAMILY);

    memset(&hints, 0, sizeof hints);

    hints.ai_family = AF_INET;
    hints.ai_flags  = 0x8000;

    CHECK(getaddrinfo("192.0.2.1", "80", &hints, &result) == EAI_BADFLAGS);
    CHECK(gai_strerror(EAI_NONAME) != NULL);
    CHECK(gai_strerror(12345) != NULL);

    freeaddrinfo(NULL);
}

static void check_getnameinfo(void) {
    struct sockaddr_in address;

    char host[NI_MAXHOST];
    char service[NI_MAXSERV];

    memset(&address, 0, sizeof address);

    address.sin_family = AF_INET;
    address.sin_port   = htons(8080);

    inet_pton(AF_INET, "192.0.2.1", &address.sin_addr);

    CHECK(getnameinfo((const struct sockaddr*) &address, sizeof address, host,
                      sizeof host, service, sizeof service,
                      NI_NUMERICHOST | NI_NUMERICSERV) == 0);
    CHECK(strcmp(host, "192.0.2.1") == 0);
    CHECK(strcmp(service, "8080") == 0);

    char small[4];

    CHECK(getnameinfo((const struct sockaddr*) &address, sizeof address, small,
                      sizeof small, NULL, 0, NI_NUMERICHOST) == EAI_OVERFLOW);
    CHECK(getnameinfo((const struct sockaddr*) &address, sizeof address, host,
                      sizeof host, service, sizeof service, NI_NAMEREQD) == EAI_NONAME);
}

int main(void) {
    check_ipv4();
    check_ipv6();
    check_defaults_and_errors();
    check_getnameinfo();

    return failures != 0;
}
