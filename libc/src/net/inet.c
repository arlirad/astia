#include <arpa/inet.h>
#include <netinet/in.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static int hex_value(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';

    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;

    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;

    return -1;
}

/* Strict dotted quad: four decimal octets, no leading zeros, no spaces. */
static int parse_v4(const char* text, unsigned char out[4]) {
    for (int part = 0; part < 4; ++part) {
        unsigned value  = 0;
        int      digits = 0;

        while (text[digits] >= '0' && text[digits] <= '9') {
            value = value * 10u + (unsigned) (text[digits] - '0');
            if (value > 255)
                return 0;

            ++digits;
        }

        if (digits == 0 || (digits > 1 && text[0] == '0'))
            return 0;

        out[part] = (unsigned char) value;
        text += digits;

        if (part != 3) {
            if (*text != '.')
                return 0;

            ++text;
        }
    }

    return *text == '\0';
}

static int parse_hex_group(const char* text, int* digits, uint16_t* value) {
    unsigned result = 0;
    int      count  = 0;
    int      digit;

    while ((digit = hex_value(text[count])) >= 0) {
        if (count == 4)
            return 0;

        result = (result << 4) | (unsigned) digit;

        ++count;
    }

    if (count == 0)
        return 0;

    *digits = count;
    *value  = (uint16_t) result;

    return 1;
}

static int parse_v6(const char* text, unsigned char out[16]) {
    uint16_t words[8];

    int count = 0;
    int gap   = -1;

    const char* cursor = text;

    if (*cursor == ':') {
        if (cursor[1] != ':')
            return 0;

        gap = 0;
        cursor += 2;
    }

    while (*cursor != '\0') {
        int digits;

        uint16_t value;

        if (count == 8)
            return 0;

        if (!parse_hex_group(cursor, &digits, &value))
            return 0;

        if (cursor[digits] == '.') {
            unsigned char v4[4];

            if (count > 6 || !parse_v4(cursor, v4))
                return 0;

            words[count++] = (uint16_t) ((v4[0] << 8) | v4[1]);
            words[count++] = (uint16_t) ((v4[2] << 8) | v4[3]);

            break;
        }

        words[count++] = value;
        cursor += digits;

        if (*cursor == '\0')
            break;

        if (*cursor != ':')
            return 0;

        ++cursor;

        if (*cursor == ':') {
            if (gap >= 0)
                return 0;

            gap = count;

            ++cursor;

            if (*cursor == '\0')
                break;
        }
        else if (*cursor == '\0') {
            return 0;
        }
    }

    if (gap >= 0) {
        const int zeros = 8 - count;
        if (zeros < 1)
            return 0;

        for (int i = count - 1; i >= gap; --i) {
            words[i + zeros] = words[i];
        }

        for (int i = 0; i < zeros; ++i) {
            words[gap + i] = 0;
        }
    }
    else if (count != 8) {
        return 0;
    }

    for (int i = 0; i < 8; ++i) {
        out[i * 2]     = (unsigned char) (words[i] >> 8);
        out[i * 2 + 1] = (unsigned char) (words[i] & 0xff);
    }

    return 1;
}

/* Legacy inet_aton syntax: 1 to 4 parts, decimal/octal/hex per part. */
static int parse_aton(const char* text, uint32_t* address) {
    uint32_t parts[4];

    int count = 0;

    const char* cursor = text;

    for (;;) {
        uint32_t value = 0;

        int base   = 10;
        int digits = 0;

        if (count == 4)
            return 0;

        if (cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X')) {
            base = 16;
            cursor += 2;
        }
        else if (cursor[0] == '0') {
            base = 8;
        }

        for (;;) {
            int digit = base == 16 ? hex_value(*cursor) : -1;

            if (base != 16 && *cursor >= '0' && *cursor <= '9')
                digit = *cursor - '0';

            if (digit < 0 || digit >= base)
                break;

            if (value > (0xffffffffu - (uint32_t) digit) / (uint32_t) base)
                return 0;

            value = value * (uint32_t) base + (uint32_t) digit;

            ++digits;
            ++cursor;
        }

        if (digits == 0)
            return 0;

        parts[count++] = value;

        if (*cursor == '.') {
            ++cursor;

            if (*cursor == '\0')
                return 0;

            continue;
        }

        break;
    }

    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r' ||
           *cursor == '\f' || *cursor == '\v') {
        ++cursor;
    }

    if (*cursor != '\0')
        return 0;

    for (int i = 0; i < count - 1; ++i) {
        if (parts[i] > 255)
            return 0;
    }

    const uint32_t limit = count == 1   ? 0xffffffffu
                           : count == 2 ? 0x00ffffffu
                           : count == 3 ? 0x0000ffffu
                                        : 0x000000ffu;

    if (parts[count - 1] > limit)
        return 0;

    uint32_t result = 0;

    for (int i = 0; i < count - 1; ++i) {
        result |= parts[i] << (8 * (3 - i));
    }

    result |= parts[count - 1];
    *address = result;

    return 1;
}

static char* append_decimal(char* cursor, unsigned value) {
    char digits[10];
    int  count = 0;

    if (value == 0) {
        *cursor++ = '0';
    }
    else {
        while (value != 0) {
            digits[count++] = (char) ('0' + value % 10u);
            value /= 10u;
        }

        while (count > 0) {
            *cursor++ = digits[--count];
        }
    }

    return cursor;
}

static char* append_hex(char* cursor, uint16_t value) {
    static const char digits[] = "0123456789abcdef";
    char              buffer[4];
    int               count = 0;

    if (value == 0) {
        *cursor++ = '0';

        return cursor;
    }

    while (value != 0) {
        buffer[count++] = digits[value & 0xfu];
        value >>= 4;
    }

    while (count > 0) {
        *cursor++ = buffer[--count];
    }

    return cursor;
}

static const char* finish_ntop(const char* buffer, size_t length, char* text,
                               socklen_t size) {
    if (length + 1 > (size_t) size) {
        errno = ENOSPC;

        return NULL;
    }

    memcpy(text, buffer, length + 1);

    return text;
}

static const char* ntop4(const void* address, char* text, socklen_t size) {
    const unsigned char* bytes = address;

    char buffer[INET_ADDRSTRLEN];

    char* cursor = buffer;

    cursor    = append_decimal(cursor, bytes[0]);
    *cursor++ = '.';
    cursor    = append_decimal(cursor, bytes[1]);
    *cursor++ = '.';
    cursor    = append_decimal(cursor, bytes[2]);
    *cursor++ = '.';
    cursor    = append_decimal(cursor, bytes[3]);
    *cursor   = '\0';

    return finish_ntop(buffer, (size_t) (cursor - buffer), text, size);
}

static const char* ntop6(const void* address, char* text, socklen_t size) {
    const unsigned char* bytes = address;
    uint16_t             words[8];

    int  best_base = -1;
    int  best_len  = 0;
    int  run_base  = -1;
    int  run_len   = 0;
    char buffer[INET6_ADDRSTRLEN];

    char* cursor = buffer;

    for (int i = 0; i < 8; ++i) {
        words[i] = (uint16_t) (((uint16_t) bytes[i * 2] << 8) | bytes[i * 2 + 1]);
    }

    for (int i = 0; i < 8; ++i) {
        if (words[i] == 0) {
            if (run_base < 0) {
                run_base = i;
                run_len  = 1;
            }
            else {
                ++run_len;
            }
        }
        else {
            if (run_base >= 0 && run_len > best_len) {
                best_base = run_base;
                best_len  = run_len;
            }

            run_base = -1;
            run_len  = 0;
        }
    }

    if (run_base >= 0 && run_len > best_len) {
        best_base = run_base;
        best_len  = run_len;
    }

    if (best_len < 2)
        best_base = -1;

    for (int i = 0; i < 8; ++i) {
        if (best_base >= 0 && i >= best_base && i < best_base + best_len) {
            if (i == best_base)
                *cursor++ = ':';

            continue;
        }

        if (i != 0)
            *cursor++ = ':';

        const int v4_tail = i == 6 && best_base == 0 &&
                            (best_len == 6 || (best_len == 7 && words[7] != 0x0001) ||
                             (best_len == 5 && words[5] == 0xffff));
        if (v4_tail) {
            cursor    = append_decimal(cursor, bytes[12]);
            *cursor++ = '.';
            cursor    = append_decimal(cursor, bytes[13]);
            *cursor++ = '.';
            cursor    = append_decimal(cursor, bytes[14]);
            *cursor++ = '.';
            cursor    = append_decimal(cursor, bytes[15]);

            break;
        }

        cursor = append_hex(cursor, words[i]);
    }

    if (best_base >= 0 && best_base + best_len == 8)
        *cursor++ = ':';

    *cursor = '\0';

    return finish_ntop(buffer, (size_t) (cursor - buffer), text, size);
}

int inet_pton(int family, const char* text, void* address) {
    if (text == NULL || address == NULL) {
        errno = EINVAL;

        return -1;
    }

    switch (family) {
    case AF_INET: {
        unsigned char bytes[4];

        if (!parse_v4(text, bytes))
            return 0;

        memcpy(address, bytes, sizeof bytes);

        return 1;
    }
    case AF_INET6: {
        unsigned char bytes[16];

        if (!parse_v6(text, bytes))
            return 0;

        memcpy(address, bytes, sizeof bytes);

        return 1;
    }
    default:
        errno = EAFNOSUPPORT;

        return -1;
    }
}

const char* inet_ntop(int family, const void* address, char* text, socklen_t size) {
    if (address == NULL || text == NULL) {
        errno = EINVAL;

        return NULL;
    }

    switch (family) {
    case AF_INET:
        return ntop4(address, text, size);
    case AF_INET6:
        return ntop6(address, text, size);
    default:
        errno = EAFNOSUPPORT;

        return NULL;
    }
}

int inet_aton(const char* text, struct in_addr* address) {
    uint32_t value;

    if (text == NULL || address == NULL) {
        errno = EINVAL;

        return 0;
    }

    if (!parse_aton(text, &value))
        return 0;

    address->s_addr = htonl(value);

    return 1;
}

in_addr_t inet_addr(const char* text) {
    struct in_addr address;

    if (text != NULL && inet_aton(text, &address))
        return address.s_addr;

    return INADDR_NONE;
}

in_addr_t inet_network(const char* text) {
    struct in_addr address;

    if (text != NULL && inet_aton(text, &address))
        return ntohl(address.s_addr);

    return INADDR_NONE;
}

char* inet_ntoa(struct in_addr address) {
    static char text[INET_ADDRSTRLEN];

    return (char*) inet_ntop(AF_INET, &address, text, sizeof text);
}
