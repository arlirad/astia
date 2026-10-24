#pragma once

#include <astia/optional.hpp>
#include <astia/api/errno.h>

namespace astia {

    /// Compile-time selected conversion from an ABI scalar to a validated
    /// domain type. Subsystems provide specializations in their mapping.hpp.
    template <typename T>
    struct mapper;

    template <typename T, u64 ValidMask>
    struct bitmask_mapper {
        static optional<T> from(u64 raw) {
            if (raw & ~ValidMask)
                return optional<T>(error_t::einval);
            return optional<T>(static_cast<T>(raw));
        }
    };

    constexpr i64 errno_value(error_t error) {
        switch (error) {
        case error_t::enone: return 0;
        case error_t::eacces: return EACCES;
        case error_t::eaddrinuse: return EADDRINUSE;
        case error_t::eagain: return EAGAIN;
        case error_t::ebadf: return EBADF;
        case error_t::ebusy: return EBUSY;
        case error_t::edom: return EDOM;
        case error_t::eexist: return EEXIST;
        case error_t::efault: return EFAULT;
        case error_t::eio: return EIO;
        case error_t::einval: return EINVAL;
        case error_t::enoent: return ENOENT;
        case error_t::enomem: return ENOMEM;
        case error_t::enospc: return ENOSPC;
        case error_t::enosys: return ENOSYS;
        case error_t::enotdir: return ENOTDIR;
        case error_t::enotempty: return ENOTEMPTY;
        case error_t::eperm: return EPERM;
        case error_t::erange: return ERANGE;
        case error_t::erofs: return EROFS;
        case error_t::etimedout: return ETIMEDOUT;
        }
        return EINVAL;
    }

} // namespace astia
