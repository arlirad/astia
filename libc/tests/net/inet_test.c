#include <arpa/inet.h>
#include <netinet/in.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition)                                                          \
    do {                                                                          \
        if (!(condition)) {                                                       \
            fprintf(stderr, "inet check failed: %s:%d: %s\n", __FILE__, __LINE__, \
                    #condition);                                                  \
            ++failures;                                                           \
        }                                                                         \
    } while (0)

_Static_assert(sizeof(struct sockaddr_in) == 16, "sockaddr_in layout");
_Static_assert(offsetof(struct sockaddr_in, sin_addr) == 4, "sockaddr_in address offset");
_Static_assert(sizeof(struct sockaddr_in6) == 28, "sockaddr_in6 layout");
_Static_assert(offsetof(struct sockaddr_in6, sin6_addr) == 8,
               "sockaddr_in6 address offset");
_Static_assert(sizeof(struct sockaddr_storage) == 128, "sockaddr_storage layout");
_Static_assert(sizeof(struct in6_addr) == 16, "in6_addr layout");

static void check_v4_text(void) {
    static const struct {
        const char* text;
        uint32_t    host_order;
    } valid[] = {
        {"0.0.0.0", 0x00000000u},
        {"255.255.255.255", 0xffffffffu},
        {"127.0.0.1", 0x7f000001u},
        {"192.0.2.1", 0xc0000201u},
    };

    static const char* invalid[] = {
        "",         "1.2.3",    "1.2.3.4.5", "256.0.0.1", "1.2.3.256", "01.2.3.4",
        "1.2.3.04", "1..2.3",   ".1.2.3",    "1.2.3.",    " 1.2.3.4",  "1.2.3.4 ",
        "-1.2.3.4", "+1.2.3.4", "1.2.3.4x",  "1.2.3.0x4",
    };

    for (size_t i = 0; i < sizeof valid / sizeof valid[0]; ++i) {
        struct in_addr address;

        memset(&address, 0, sizeof address);

        CHECK(inet_pton(AF_INET, valid[i].text, &address) == 1);
        CHECK(ntohl(address.s_addr) == valid[i].host_order);

        char text[INET_ADDRSTRLEN];

        CHECK(inet_ntop(AF_INET, &address, text, sizeof text) == text);
        CHECK(strcmp(text, valid[i].text) == 0);
    }

    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        struct in_addr address = {0x5a5a5a5au};

        CHECK(inet_pton(AF_INET, invalid[i], &address) == 0);
        CHECK(address.s_addr == 0x5a5a5a5au);
    }
}

static void check_v6_text(void) {
    static const struct {
        const char*   text;
        const char*   canonical;
        unsigned char bytes[16];
    } valid[] = {
        {"::", "::", {0}},
        {"::1", "::1", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}},
        {"2001:db8::1",
         "2001:db8::1",
         {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}},
        {"1:2:3:4:5:6:7:8",
         "1:2:3:4:5:6:7:8",
         {0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 7, 0, 8}},
        {"1:2:3:4:5:6:7::",
         "1:2:3:4:5:6:7:0",
         {0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 7, 0, 0}},
        {"::ffff:192.0.2.1",
         "::ffff:192.0.2.1",
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0xc0, 0x00, 0x02, 0x01}},
        {"1:2:3:4:5:6:1.2.3.4",
         "1:2:3:4:5:6:102:304",
         {0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0x01, 0x02, 0x03, 0x04}},
        {"ABCD::EF",
         "abcd::ef",
         {0xab, 0xcd, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xef}},
    };

    static const char* invalid[] = {
        "",
        ":",
        ":::",
        "1",
        "1:2:3:4:5:6:7",
        "1:2:3:4:5:6:7:8:9",
        "1::2::3",
        "12345::",
        "g::1",
        "::ffff:1.2.3.999",
        "::ffff:1.2.3",
        "1:2:3:4:5:6:7:8::",
        "1:2:3:4:5:6:7:1.2.3.4",
        "::1 ",
        " ::1",
        "1:2:3:4:5:6:7:",
    };

    for (size_t i = 0; i < sizeof valid / sizeof valid[0]; ++i) {
        struct in6_addr address;

        memset(&address, 0xa5, sizeof address);

        CHECK(inet_pton(AF_INET6, valid[i].text, &address) == 1);
        CHECK(memcmp(&address, valid[i].bytes, sizeof address) == 0);

        char text[INET6_ADDRSTRLEN];

        CHECK(inet_ntop(AF_INET6, &address, text, sizeof text) == text);
        CHECK(strcmp(text, valid[i].canonical) == 0);
    }

    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        struct in6_addr address;

        memset(&address, 0xa5, sizeof address);

        CHECK(inet_pton(AF_INET6, invalid[i], &address) == 0);
        CHECK(address.s6_addr[0] == 0xa5);
    }
}

static void check_v6_formatting(void) {
    static const struct {
        unsigned char bytes[16];
        const char*   text;
    } cases[] = {
        {{0}, "::"},
        {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}, "::1"},
        {{0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2}, "1::2"},
        {{0, 1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0}, "1:0:2:0:3:0:4:0"},
        {{0, 1, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 3, 0, 4}, "1::2:0:0:3:4"},
        {{0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0}, "0:0:0:1::"},
        {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0xc0, 0x00, 0x02, 0x01},
         "::ffff:192.0.2.1"},
        {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xc0, 0x00, 0x02, 0x01}, "::192.0.2.1"},
        {{0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}, "fe80::1"},
        {{0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 7, 0, 8}, "1:2:3:4:5:6:7:8"},
    };

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        char text[INET6_ADDRSTRLEN];

        CHECK(inet_ntop(AF_INET6, cases[i].bytes, text, sizeof text) == text);
        CHECK(strcmp(text, cases[i].text) == 0);
    }
}

static void check_buffer_limits(void) {
    struct in_addr  v4 = {htonl(0xc0000201u)};
    struct in6_addr v6 = {0};

    char text[INET6_ADDRSTRLEN];
    char tight[sizeof "192.0.2.1"];

    CHECK(inet_ntop(AF_INET, &v4, tight, sizeof tight) == tight);
    CHECK(strcmp(tight, "192.0.2.1") == 0);

    errno = 0;

    CHECK(inet_ntop(AF_INET, &v4, text, sizeof "192.0.2.1" - 1) == NULL);
    CHECK(errno == ENOSPC);

    CHECK(inet_ntop(AF_INET6, &v6, tight, sizeof tight) == tight);
    CHECK(strcmp(tight, "::") == 0);

    errno = 0;

    CHECK(inet_ntop(AF_INET6, &v6, text, 2) == NULL);
    CHECK(errno == ENOSPC);
}

static void check_legacy_forms(void) {
    static const struct {
        const char* text;
        uint32_t    bytes;
    } valid[] = {
        {"127.1", 0x7f000001u},      {"0x7f.1", 0x7f000001u},
        {"0177.0.0.1", 0x7f000001u}, {"1.2.65535", 0x0102ffffu},
        {"1.16777215", 0x01ffffffu}, {"4294967295", 0xffffffffu},
        {"1.2.3.4 ", 0x01020304u},   {"1.2.3.4\t", 0x01020304u},
    };

    static const char* invalid[] = {
        "",           "08.1",       "0x",       "1.2.3.4.5", "1.2.3.256",
        "1.16777216", "4294967296", " 1.2.3.4", "1.2.3.4x",  "1.2.3.-1",
    };

    for (size_t i = 0; i < sizeof valid / sizeof valid[0]; ++i) {
        struct in_addr address;

        CHECK(inet_aton(valid[i].text, &address) == 1);
        CHECK(ntohl(address.s_addr) == valid[i].bytes);
    }

    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        struct in_addr address = {0x5a5a5a5au};

        CHECK(inet_aton(invalid[i], &address) == 0);
        CHECK(address.s_addr == 0x5a5a5a5au);
    }

    CHECK(inet_addr("192.0.2.1") == htonl(0xc0000201u));
    CHECK(inet_addr("bad") == INADDR_NONE);
    CHECK(inet_network("1.2.3.4") == 0x01020304u);
}

static void check_ntoa_and_errors(void) {
    struct in_addr address = {htonl(0xc0000201u)};

    CHECK(strcmp(inet_ntoa(address), "192.0.2.1") == 0);

    errno = 0;

    CHECK(inet_pton(99, "1.2.3.4", &address) == -1);
    CHECK(errno == EAFNOSUPPORT);

    char text[INET6_ADDRSTRLEN];

    errno = 0;

    CHECK(inet_ntop(99, &address, text, sizeof text) == NULL);
    CHECK(errno == EAFNOSUPPORT);
}

static void check_byte_order_and_macros(void) {
    uint16_t short_value = htons(0x1234u);
    uint32_t long_value  = htonl(0x12345678u);

    unsigned char short_bytes[2];
    unsigned char long_bytes[4];

    memcpy(short_bytes, &short_value, sizeof short_bytes);
    memcpy(long_bytes, &long_value, sizeof long_bytes);

    CHECK(short_bytes[0] == 0x12 && short_bytes[1] == 0x34);
    CHECK(long_bytes[0] == 0x12 && long_bytes[1] == 0x34 && long_bytes[2] == 0x56 &&
          long_bytes[3] == 0x78);
    CHECK(ntohs(short_value) == 0x1234u);
    CHECK(ntohl(long_value) == 0x12345678u);

    struct in6_addr loopback = IN6ADDR_LOOPBACK_INIT;
    struct in6_addr any      = IN6ADDR_ANY_INIT;
    struct in6_addr link     = {{{0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}}};
    struct in6_addr mcast    = {{{0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}}};
    struct in6_addr mapped   = {
        {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0xc0, 0, 2, 1}}};

    CHECK(IN6_IS_ADDR_LOOPBACK(&loopback));
    CHECK(IN6_IS_ADDR_UNSPECIFIED(&any));
    CHECK(IN6_IS_ADDR_LINKLOCAL(&link));
    CHECK(IN6_IS_ADDR_MULTICAST(&mcast));
    CHECK(IN6_IS_ADDR_MC_LINKLOCAL(&mcast));
    CHECK(IN6_IS_ADDR_V4MAPPED(&mapped));

    CHECK(IN_CLASSA(0x7f000001u));
    CHECK(IN_CLASSB(0x80000001u));
    CHECK(IN_CLASSC(0xc0000201u));
    CHECK(IN_MULTICAST(0xe0000001u));
    CHECK(IN_LOOPBACK(0x7f000001u));
}

int main(void) {
    check_v4_text();
    check_v6_text();
    check_v6_formatting();
    check_buffer_limits();
    check_legacy_forms();
    check_ntoa_and_errors();
    check_byte_order_and_macros();

    return failures != 0;
}
