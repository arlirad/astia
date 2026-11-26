#pragma once

#include <stdint.h>

struct __astia_socket_msghdr {
    void*    control;
    uint32_t controllen;
    uint32_t flags;
    uint32_t msg_flags;
    uint32_t resv;
};
