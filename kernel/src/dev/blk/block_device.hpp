#pragma once

#include <astia/common.hpp>

namespace astia::dev::blk {

    inline constexpr u32 block_device_abi_version = 1;

    struct block_device_ops {
        u32  abi_version;
        bool (*read_at)(void* context, u64 offset, void* buffer, usize length);
    };

    struct block_device {
        const block_device_ops* ops       = nullptr;
        void*                   context   = nullptr;
        u64                     size_bytes = 0;
        u32                     block_size = 0;
    };

    bool valid(const block_device& device);
    bool read_at(const block_device& device, u64 offset, void* buffer, usize length);
    bool read_at_adapter(void* context, u64 offset, void* buffer, usize length);

} // namespace astia::dev::blk
