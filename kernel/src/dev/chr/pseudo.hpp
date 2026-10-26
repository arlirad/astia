#pragma once

#include "chrdev.hpp"

namespace astia::dev {

    class null_device final : public chrdev {
        public:
        constexpr null_device() : chrdev("null", dv_char | dv_sync) {}

        long read(void*, u32, u64) override;
        long write(const void*, u32 length, u64) override;
    };

    class zero_device final : public chrdev {
        public:
        constexpr zero_device() : chrdev("zero", dv_char | dv_sync) {}

        long read(void* buffer, u32 length, u64) override;
        long write(const void*, u32 length, u64) override;
    };

    /// Register the kernel-owned, host-independent pseudo devices in devfs.
    bool pseudo_devices_init();

} // namespace astia::dev
