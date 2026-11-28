#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <errno.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int known_flags = AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST |
                               AI_NUMERICSERV | AI_ADDRCONFIG | AI_V4MAPPED | AI_ALL;

struct service_entry {
    const char* name;
    uint16_t    port;
};

static const struct service_entry services[] = {
    {"ftp", 21},        {"ssh", 22},         {"telnet", 23}, {"smtp", 25},
    {"domain", 53},     {"http", 80},        {"pop3", 110},  {"imap", 143},
    {"https", 443},     {"submission", 587}, {"imaps", 993}, {"pop3s", 995},
    {"http-alt", 8080},
};

static char lower(char value) {
    return value >= 'A' && value <= 'Z' ? (char) (value - 'A' + 'a') : value;
}

static int equal_ignore_case(const char* left, const char* right) {
    while (*left != '\0' && *right != '\0') {
        if (lower(*left) != lower(*right))
            return 0;

        ++left;
        ++right;
    }

    return *left == '\0' && *right == '\0';
}

static int parse_number(const char* text, uint16_t* value) {
    uint32_t result = 0;

    int digits = 0;

    if (text == NULL || *text == '\0')
        return 0;

    while (*text != '\0') {
        if (*text < '0' || *text > '9' || digits == 5)
            return 0;

        result = result * 10u + (uint32_t) (*text - '0');

        ++digits;
        ++text;
    }

    if (result > 65535u)
        return 0;

    *value = (uint16_t) result;

    return 1;
}

static int parse_service(const char* service, int flags, uint16_t* port) {
    *port = 0;

    if (service == NULL)
        return 0;

    if (parse_number(service, port))
        return 0;

    if ((flags & AI_NUMERICSERV) != 0)
        return EAI_SERVICE;

    for (size_t i = 0; i < sizeof services / sizeof services[0]; ++i) {
        if (equal_ignore_case(service, services[i].name)) {
            *port = services[i].port;

            return 0;
        }
    }

    return EAI_SERVICE;
}

static int parse_node_numeric(const char* node, int family, unsigned char address[16],
                              int* resolved_family) {
    if (family != AF_INET6 && inet_pton(AF_INET, node, address) == 1) {
        *resolved_family = AF_INET;

        return 1;
    }

    if (family != AF_INET && inet_pton(AF_INET6, node, address) == 1) {
        *resolved_family = AF_INET6;

        return 1;
    }

    return 0;
}

static int hosts_lookup(const char* node, int family, unsigned char address[16],
                        int* resolved_family) {
    FILE* hosts = fopen("/etc/hosts", "r");
    if (hosts == NULL)
        return 0;

    char line[512];

    while (fgets(line, sizeof line, hosts) != NULL) {
        char* cursor = line;

        while (*cursor == ' ' || *cursor == '\t') {
            ++cursor;
        }

        if (*cursor == '#' || *cursor == '\n' || *cursor == '\0')
            continue;

        char* address_text = cursor;

        while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t' && *cursor != '\n') {
            ++cursor;
        }

        if (*cursor != '\0')
            *cursor++ = '\0';

        unsigned char candidate[16];
        int           candidate_family = 0;

        if (family != AF_INET6 && inet_pton(AF_INET, address_text, candidate) == 1)
            candidate_family = AF_INET;
        else if (family != AF_INET && inet_pton(AF_INET6, address_text, candidate) == 1)
            candidate_family = AF_INET6;
        else
            continue;

        while (*cursor != '\0' && *cursor != '\n') {
            while (*cursor == ' ' || *cursor == '\t') {
                ++cursor;
            }

            if (*cursor == '#' || *cursor == '\0' || *cursor == '\n')
                break;

            char* name = cursor;

            while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t' &&
                   *cursor != '\n' && *cursor != '#') {
                ++cursor;
            }

            const char terminator = *cursor;

            *cursor = '\0';

            if (equal_ignore_case(name, node)) {
                memcpy(address, candidate, sizeof candidate);

                *resolved_family = candidate_family;

                fclose(hosts);

                return 1;
            }

            if (terminator == '\0' || terminator == '\n' || terminator == '#')
                break;

            ++cursor;
        }
    }

    fclose(hosts);

    return 0;
}

static struct addrinfo* make_result(int family, const unsigned char address[16],
                                    uint16_t port, int socktype,
                                    const char* canonical_name) {
    struct addrinfo* result = (struct addrinfo*) malloc(sizeof *result);
    if (result == NULL)
        return NULL;

    memset(result, 0, sizeof *result);

    result->ai_family   = family;
    result->ai_socktype = socktype;
    result->ai_protocol = socktype == SOCK_DGRAM ? IPPROTO_UDP : IPPROTO_TCP;

    if (family == AF_INET) {
        struct sockaddr_in* address_in = (struct sockaddr_in*) malloc(sizeof *address_in);
        if (address_in == NULL) {
            free(result);

            return NULL;
        }

        memset(address_in, 0, sizeof *address_in);

        address_in->sin_family = AF_INET;
        address_in->sin_port   = htons(port);

        memcpy(&address_in->sin_addr, address, 4);

        result->ai_addr    = (struct sockaddr*) address_in;
        result->ai_addrlen = sizeof *address_in;
    }
    else {
        struct sockaddr_in6* address_in6 =
            (struct sockaddr_in6*) malloc(sizeof *address_in6);
        if (address_in6 == NULL) {
            free(result);

            return NULL;
        }

        memset(address_in6, 0, sizeof *address_in6);

        address_in6->sin6_family = AF_INET6;
        address_in6->sin6_port   = htons(port);

        memcpy(&address_in6->sin6_addr, address, 16);

        result->ai_addr    = (struct sockaddr*) address_in6;
        result->ai_addrlen = sizeof *address_in6;
    }

    if (canonical_name != NULL) {
        const size_t length = strlen(canonical_name) + 1;

        result->ai_canonname = (char*) malloc(length);

        if (result->ai_canonname == NULL) {
            free(result->ai_addr);
            free(result);

            return NULL;
        }

        memcpy(result->ai_canonname, canonical_name, length);
    }

    return result;
}

static void release_chain(struct addrinfo* result) {
    while (result != NULL) {
        struct addrinfo* next = result->ai_next;

        free(result->ai_canonname);
        free(result->ai_addr);
        free(result);

        result = next;
    }
}

static int getnameinfo_numeric(const struct sockaddr* address, char* host,
                               socklen_t host_length, char* service,
                               socklen_t service_length) {
    char numeric_host[INET6_ADDRSTRLEN];

    const char* host_result = NULL;
    uint16_t    port        = 0;

    if (address->sa_family == AF_INET) {
        const struct sockaddr_in* address_in = (const struct sockaddr_in*) address;

        host_result =
            inet_ntop(AF_INET, &address_in->sin_addr, numeric_host, sizeof numeric_host);
        port = ntohs(address_in->sin_port);
    }
    else if (address->sa_family == AF_INET6) {
        const struct sockaddr_in6* address_in6 = (const struct sockaddr_in6*) address;

        host_result = inet_ntop(AF_INET6, &address_in6->sin6_addr, numeric_host,
                                sizeof numeric_host);
        port        = ntohs(address_in6->sin6_port);
    }
    else {
        return EAI_FAMILY;
    }

    if (host_result == NULL)
        return EAI_FAMILY;

    if (host != NULL) {
        const size_t length = strlen(host_result) + 1;
        if (length > host_length)
            return EAI_OVERFLOW;

        memcpy(host, host_result, length);
    }

    if (service != NULL) {
        char digits[6];

        char* cursor = digits + sizeof digits;

        *--cursor = '\0';

        if (port == 0) {
            *--cursor = '0';
        }
        else {
            uint16_t value = port;

            while (value != 0) {
                *--cursor = (char) ('0' + value % 10u);
                value /= 10u;
            }
        }

        const size_t length = strlen(cursor) + 1;
        if (length > service_length)
            return EAI_OVERFLOW;

        memcpy(service, cursor, length);
    }

    return 0;
}

int getaddrinfo(const char* node, const char* service, const struct addrinfo* hints,
                struct addrinfo** result) {
    const int flags    = hints == NULL ? 0 : hints->ai_flags;
    const int family   = hints == NULL ? AF_UNSPEC : hints->ai_family;
    const int socktype = hints == NULL ? 0 : hints->ai_socktype;
    const int protocol = hints == NULL ? 0 : hints->ai_protocol;

    if (result == NULL)
        return EAI_FAIL;

    *result = NULL;

    if ((flags & ~known_flags) != 0)
        return EAI_BADFLAGS;

    if (family != AF_UNSPEC && family != AF_INET && family != AF_INET6)
        return EAI_FAMILY;

    if (socktype != 0 && socktype != SOCK_STREAM && socktype != SOCK_DGRAM)
        return EAI_SOCKTYPE;

    if (protocol != 0 && protocol != IPPROTO_TCP && protocol != IPPROTO_UDP)
        return EAI_SOCKTYPE;

    uint16_t port;

    const int service_error = parse_service(service, flags, &port);

    if (service_error != 0)
        return service_error;

    unsigned char address[16]    = {0};
    int           address_family = AF_INET;

    if (node == NULL) {
        if (family == AF_INET6) {
            address[15]    = (flags & AI_PASSIVE) != 0 ? 0 : 1;
            address_family = AF_INET6;
        }
        else if ((flags & AI_PASSIVE) == 0) {
            address[0] = 127;
            address[3] = 1;
        }
    }
    else if (parse_node_numeric(node, family, address, &address_family)) {
    }
    else if ((flags & AI_NUMERICHOST) != 0) {
        return EAI_NONAME;
    }
    else if (!hosts_lookup(node, family, address, &address_family)) {
        return EAI_NONAME;
    }

    int socket_types[2];
    int socket_type_count = 0;

    if (socktype == 0) {
        socket_types[socket_type_count++] = SOCK_STREAM;
        socket_types[socket_type_count++] = SOCK_DGRAM;
    }
    else {
        socket_types[socket_type_count++] = socktype;
    }

    struct addrinfo* head = NULL;
    struct addrinfo* tail = NULL;

    for (int i = 0; i < socket_type_count; ++i) {
        const char* canonical_name =
            i == 0 && (flags & AI_CANONNAME) != 0 && node != NULL ? node : NULL;
        struct addrinfo* entry =
            make_result(address_family, address, port, socket_types[i], canonical_name);

        if (entry == NULL) {
            release_chain(head);

            return EAI_MEMORY;
        }

        if (tail == NULL)
            head = entry;
        else
            tail->ai_next = entry;

        tail = entry;
    }

    *result = head;

    return 0;
}

void freeaddrinfo(struct addrinfo* result) {
    release_chain(result);
}

const char* gai_strerror(int error) {
    switch (error) {
    case 0:
        return "Success";
    case EAI_BADFLAGS:
        return "Invalid value for ai_flags";
    case EAI_NONAME:
        return "Name or service not known";
    case EAI_AGAIN:
        return "Temporary failure in name resolution";
    case EAI_FAIL:
        return "Non-recoverable failure in name resolution";
    case EAI_FAMILY:
        return "ai_family not supported";
    case EAI_SOCKTYPE:
        return "ai_socktype not supported";
    case EAI_SERVICE:
        return "Servname not supported for ai_socktype";
    case EAI_MEMORY:
        return "Memory allocation failure";
    case EAI_SYSTEM:
        return "System error";
    case EAI_OVERFLOW:
        return "Argument buffer overflow";
    default:
        return "Unknown error";
    }
}

int getnameinfo(const struct sockaddr* address, socklen_t address_length, char* host,
                socklen_t host_length, char* service, socklen_t service_length,
                int flags) {
    (void) address_length;

    if (address == NULL)
        return EAI_FAIL;

    if ((flags &
         ~(NI_NOFQDN | NI_NUMERICHOST | NI_NAMEREQD | NI_NUMERICSERV | NI_DGRAM)) != 0)
        return EAI_BADFLAGS;

    const int rc =
        getnameinfo_numeric(address, host, host_length, service, service_length);
    if (rc != 0)
        return rc;

    if ((flags & NI_NAMEREQD) != 0 && (flags & NI_NUMERICHOST) == 0)
        return EAI_NONAME;

    return 0;
}

int h_errno;

struct hostent* gethostbyname(const char* name) {
    static _Thread_local char           address_storage[16];
    static _Thread_local char*          address_list[2];
    static _Thread_local struct hostent entry;

    struct addrinfo hints;

    memset(&hints, 0, sizeof hints);

    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* result = NULL;

    if (name == NULL || getaddrinfo(name, NULL, &hints, &result) != 0 || result == NULL) {
        h_errno = HOST_NOT_FOUND;

        return NULL;
    }

    const struct sockaddr_in* address = (const struct sockaddr_in*) result->ai_addr;

    memcpy(address_storage, &address->sin_addr, 4);

    address_list[0] = address_storage;
    address_list[1] = NULL;

    entry.h_name      = (char*) name;
    entry.h_aliases   = NULL;
    entry.h_addrtype  = AF_INET;
    entry.h_length    = 4;
    entry.h_addr_list = address_list;

    freeaddrinfo(result);

    return &entry;
}
