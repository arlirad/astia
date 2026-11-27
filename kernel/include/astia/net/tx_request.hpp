#pragma once

#include <astia/common.hpp>

namespace astia::net {
    struct tx_request;
    using tx_reclaim_fn = void (*)(void*, const tx_request*, int);

    struct tx_request {
        const u8*     data       = nullptr;
        u64           physical   = 0;
        u32           length     = 0;
        u64           generation = 0;
        void*         cookie     = nullptr;
        tx_reclaim_fn reclaim    = nullptr;
    };
} // namespace astia::net
