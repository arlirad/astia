#pragma once

/*
 * astia/api/errno.h — error codes of the astia userspace ABI.
 *
 * Convention: a direct syscall return or cqe.result value >= 0 is success
 * (payload: bytes, fd, pid, revents); a negative value is one of these
 * codes negated (-ENOENT, -EINVAL, ...).  The ABI returns errors directly,
 * so there is no thread-local errno storage.
 *
 * Numeric assignment follows the familiar POSIX/Linux set so muscle memory
 * works; it is not a compatibility promise with either.
 *
 * Unprefixed by design: the <astia/api/...> include path already provides
 * the namespace, like a system <errno.h>.
 */

#ifdef __cplusplus
extern "C" {
#endif

enum {
    EPERM           = 1,
    ENOENT          = 2,
    ESRCH           = 3,
    EINTR           = 4,
    EIO             = 5,
    ENXIO           = 6,
    E2BIG           = 7,
    ENOEXEC         = 8,
    EBADF           = 9,
    ECHILD          = 10,
    EAGAIN          = 11,
    ENOMEM          = 12,
    EACCES          = 13,
    EFAULT          = 14,
    EBUSY           = 16,
    EEXIST          = 17,
    EXDEV           = 18,
    ENODEV          = 19,
    ENOTDIR         = 20,
    EISDIR          = 21,
    EINVAL          = 22,
    ENFILE          = 23,
    EMFILE          = 24,
    ENOTTY          = 25,
    EFBIG           = 27,
    ENOSPC          = 28,
    ESPIPE          = 29,
    EROFS           = 30,
    EPIPE           = 32,
    EDOM            = 33,
    ERANGE          = 34,
    EDEADLK         = 35,
    ENAMETOOLONG    = 36,
    ENOSYS          = 38,
    ENOTSOCK        = 88,
    EPROTOTYPE      = 91,
    ENOPROTOOPT     = 92,
    EPROTONOSUPPORT = 93,
    ENOTEMPTY       = 39,
    ELOOP           = 40,
    EOVERFLOW       = 75,
    EADDRINUSE      = 98,
    ENOTCONN        = 107,
    EISCONN         = 106,
    ECONNRESET      = 104,
    ECONNREFUSED    = 111,
    EAFNOSUPPORT    = 97,
    EILSEQ          = 84,
    ENOTSUP         = 95,
    ETIMEDOUT       = 110,
    EINPROGRESS     = 115,
    EALREADY        = 114,
    ECANCELED       = 125,
};

/* conventional aliases */
#define EWOULDBLOCK EAGAIN
#define EOPNOTSUPP ENOTSUP

#ifdef __cplusplus
} /* extern "C" */
#endif
