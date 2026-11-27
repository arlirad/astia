#pragma once

#include <astia/api/errno.h>
#include <astia/arch/cpu.hpp>
#include <astia/atomic.hpp>
#include <astia/common.hpp>
#include <astia/core/mpsc_queue.hpp>
#include <astia/core/spsc_queue.hpp>
#include <astia/mm/shptr.hpp>
#include <astia/net/address.hpp>
#include <astia/net/packet_view.hpp>
#include <astia/net/tx_path.hpp>
#include <astia/net/tx_request.hpp>
#include <astia/net/types.hpp>

namespace astia::net {
    class link;

    enum link_flags : u32 {
        link_up = 1u << 0,
    };
} // namespace astia::net
