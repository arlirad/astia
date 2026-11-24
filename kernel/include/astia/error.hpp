#pragma once

#include <astia/api/errno.h>
#include <astia/common.hpp>

namespace astia {

    /// Kernel error codes in the POSIX errno tradition, lower-cased to match
    /// the project's snake_case naming rule. The numeric values are stable so
    /// they can later cross the syscall boundary.
    enum class error_t : u32 {
        enone = 0,  ///< Success.
        eacces,     ///< Permission denied.
        eaddrinuse, ///< Address already in use.
        eagain,     ///< Resource temporarily unavailable.
        ebadf,      ///< Bad file descriptor.
        ebusy,      ///< Resource busy.
        edom,       ///< Argument outside the valid domain.
        eexist,     ///< Entry already exists.
        efault,     ///< Bad address.
        eio,        ///< I/O error.
        einval,     ///< Invalid argument.
        enoent,     ///< No such entry.
        enomem,     ///< Out of memory.
        emfile,     ///< Process file descriptor limit reached.
        enospc,     ///< No space left.
        enosys,     ///< Function not implemented.
        enotdir,    ///< Not a directory.
        enotempty,  ///< Directory not empty.
        eperm,      ///< Operation not permitted.
        erange,     ///< Result out of range.
        erofs,      ///< Read-only storage.
        etimedout,  ///< Timed out.
        e2big,      ///< Argument or vector exceeds a fixed contract.
        enfile,     ///< Kernel-wide finite resource table is full.
        ecanceled,  ///< Operation was cancelled before it completed.
    };

    /// Convert to the negative errno form used by the VFS and syscall seam.
    constexpr i64 error_errno(error_t error) {
        switch (error) {
        case error_t::enone:
            return 0;
        case error_t::eacces:
            return -EACCES;
        case error_t::eaddrinuse:
            return -EADDRINUSE;
        case error_t::eagain:
            return -EAGAIN;
        case error_t::ebadf:
            return -EBADF;
        case error_t::ebusy:
            return -EBUSY;
        case error_t::edom:
            return -EDOM;
        case error_t::eexist:
            return -EEXIST;
        case error_t::efault:
            return -EFAULT;
        case error_t::eio:
            return -EIO;
        case error_t::einval:
            return -EINVAL;
        case error_t::enoent:
            return -ENOENT;
        case error_t::enomem:
            return -ENOMEM;
        case error_t::emfile:
            return -EMFILE;
        case error_t::enospc:
            return -ENOSPC;
        case error_t::enosys:
            return -ENOSYS;
        case error_t::enotdir:
            return -ENOTDIR;
        case error_t::enotempty:
            return -ENOTEMPTY;
        case error_t::eperm:
            return -EPERM;
        case error_t::erange:
            return -ERANGE;
        case error_t::erofs:
            return -EROFS;
        case error_t::etimedout:
            return -ETIMEDOUT;
        case error_t::e2big:
            return -E2BIG;
        case error_t::enfile:
            return -ENFILE;
        case error_t::ecanceled:
            return -ECANCELED;
        }
        return -EIO;
    }

    /// Short, human-readable name for an error code (for logs). Never null.
    inline const char* error_string(error_t error) {
        switch (error) {
        case error_t::enone:
            return "success";
        case error_t::eacces:
            return "permission denied";
        case error_t::eaddrinuse:
            return "address in use";
        case error_t::eagain:
            return "resource unavailable";
        case error_t::ebadf:
            return "bad descriptor";
        case error_t::ebusy:
            return "resource busy";
        case error_t::edom:
            return "domain error";
        case error_t::eexist:
            return "already exists";
        case error_t::efault:
            return "bad address";
        case error_t::eio:
            return "i/o error";
        case error_t::einval:
            return "invalid argument";
        case error_t::enoent:
            return "not found";
        case error_t::enomem:
            return "out of memory";
        case error_t::emfile:
            return "too many open files";
        case error_t::enospc:
            return "no space left";
        case error_t::enosys:
            return "not implemented";
        case error_t::enotdir:
            return "not a directory";
        case error_t::enotempty:
            return "not empty";
        case error_t::eperm:
            return "operation not permitted";
        case error_t::erange:
            return "out of range";
        case error_t::erofs:
            return "read-only";
        case error_t::etimedout:
            return "timed out";
        case error_t::e2big:
            return "argument list too large";
        case error_t::enfile:
            return "too many open files in system";
        case error_t::ecanceled:
            return "operation cancelled";
        }
        return "unknown error";
    }

} // namespace astia
