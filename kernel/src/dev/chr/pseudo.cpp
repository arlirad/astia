#include "pseudo.hpp"

#include <astia/api/errno.h>
#include <astia/crypto/csprng.hpp>
#include <astia/crypto/random.hpp>

namespace astia::dev {
    long null_device::read(void* buffer, u32 length, u64) {
        if (buffer == nullptr && length != 0)
            return -EFAULT;

        return 0;
    }

    long null_device::write(const void* buffer, u32 length, u64) {
        if (buffer == nullptr && length != 0)
            return -EFAULT;

        return static_cast<long>(length);
    }

    long zero_device::read(void* buffer, u32 length, u64) {
        if (buffer == nullptr && length != 0)
            return -EFAULT;

        auto* out = static_cast<u8*>(buffer);

        for (u32 i = 0; i < length; ++i) {
            out[i] = 0;
        }

        return static_cast<long>(length);
    }

    long zero_device::write(const void* buffer, u32 length, u64) {
        if (buffer == nullptr && length != 0)
            return -EFAULT;

        return static_cast<long>(length);
    }
} // namespace astia::dev

namespace astia::dev {
    long urandom_device::read(void* buffer, u32 length, u64) {
        if (buffer == nullptr && length != 0)
            return -EFAULT;

        if (length > crypto::csprng::max_request)
            return -EINVAL;

        if (!crypto::random_bytes(buffer, length))
            return -EAGAIN;

        return static_cast<long>(length);
    }

    long urandom_device::write(const void*, u32 length, u64) {
        return static_cast<long>(length);
    }
} // namespace astia::dev
