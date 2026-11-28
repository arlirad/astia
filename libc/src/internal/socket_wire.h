#pragma once

#include <stdint.h>

struct __astia_socket_msghdr {
    void*    control;
    uint32_t controllen;
    uint32_t flags;
    uint32_t msg_flags;
    uint32_t resv;
};

static inline void __astia_socket_msghdr_set_name(struct __astia_socket_msghdr* header,
                                                  const void*                   name) {
    uint64_t value = (uint64_t) (uintptr_t) name;

    __builtin_memcpy(&header->msg_flags, &value, sizeof value);
}

static inline const void*
__astia_socket_msghdr_name(const struct __astia_socket_msghdr* header) {
    uint64_t value;

    __builtin_memcpy(&value, &header->msg_flags, sizeof value);

    return (const void*) (uintptr_t) value;
}
