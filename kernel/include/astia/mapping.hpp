#pragma once

#include <astia/api/errno.h>
#include <astia/optional.hpp>

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
        case error_t::enone:
            return 0;
        case error_t::eacces:
            return EACCES;
        case error_t::eaddrinuse:
            return EADDRINUSE;
        case error_t::eagain:
            return EAGAIN;
        case error_t::e2big:
            return E2BIG;
        case error_t::ebadf:
            return EBADF;
        case error_t::ebusy:
            return EBUSY;
        case error_t::edom:
            return EDOM;
        case error_t::eexist:
            return EEXIST;
        case error_t::efault:
            return EFAULT;
        case error_t::eio:
            return EIO;
        case error_t::einval:
            return EINVAL;
        case error_t::enoent:
            return ENOENT;
        case error_t::enomem:
            return ENOMEM;
        case error_t::emfile:
            return EMFILE;
        case error_t::enfile:
            return ENFILE;
        case error_t::enospc:
            return ENOSPC;
        case error_t::enosys:
            return ENOSYS;
        case error_t::enotdir:
            return ENOTDIR;
        case error_t::enotempty:
            return ENOTEMPTY;
        case error_t::eperm:
            return EPERM;
        case error_t::erange:
            return ERANGE;
        case error_t::erofs:
            return EROFS;
        case error_t::etimedout:
            return ETIMEDOUT;
        case error_t::ecanceled:
            return ECANCELED;
        }
        return EINVAL;
    }

    constexpr error_t error_from_errno(i64 value) {
        switch (value) {
        case 0:
            return error_t::enone;
        case EACCES:
            return error_t::eacces;
        case EADDRINUSE:
            return error_t::eaddrinuse;
        case EAGAIN:
            return error_t::eagain;
        case E2BIG:
            return error_t::e2big;
        case EBADF:
            return error_t::ebadf;
        case EBUSY:
            return error_t::ebusy;
        case EDOM:
            return error_t::edom;
        case EEXIST:
            return error_t::eexist;
        case EFAULT:
            return error_t::efault;
        case EIO:
            return error_t::eio;
        case EINVAL:
            return error_t::einval;
        case ENOENT:
            return error_t::enoent;
        case ENOMEM:
            return error_t::enomem;
        case EMFILE:
            return error_t::emfile;
        case ENFILE:
            return error_t::enfile;
        case ENOSPC:
            return error_t::enospc;
        case ENOSYS:
            return error_t::enosys;
        case ENOTDIR:
            return error_t::enotdir;
        case ENOTEMPTY:
            return error_t::enotempty;
        case EPERM:
            return error_t::eperm;
        case ERANGE:
            return error_t::erange;
        case EROFS:
            return error_t::erofs;
        case ETIMEDOUT:
            return error_t::etimedout;
        case ECANCELED:
            return error_t::ecanceled;
        default:
            return error_t::eio;
        }
    }

} // namespace astia
