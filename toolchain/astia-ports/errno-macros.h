#pragma once

/* Astia's ABI enumerates errno values, but portable C code probes them with
 * #ifdef.  Give the preprocessor the names it expects after the enum has been
 * parsed. */
#include <errno.h>

#ifndef EAGAIN
#define EAGAIN 11
#endif
#ifndef EALREADY
#define EALREADY 114
#endif
#ifndef EINPROGRESS
#define EINPROGRESS 115
#endif
#ifndef ECONNRESET
#define ECONNRESET 104
#endif
#ifndef ECONNREFUSED
#define ECONNREFUSED 111
#endif
#ifndef ETIMEDOUT
#define ETIMEDOUT 110
#endif
#ifndef ENOTCONN
#define ENOTCONN 107
#endif
#ifndef EISCONN
#define EISCONN 106
#endif
#ifndef EADDRINUSE
#define EADDRINUSE 98
#endif
#ifndef EAFNOSUPPORT
#define EAFNOSUPPORT 97
#endif
#ifndef ENOPROTOOPT
#define ENOPROTOOPT 92
#endif
#ifndef EPROTOTYPE
#define EPROTOTYPE 91
#endif
#ifndef EPROTONOSUPPORT
#define EPROTONOSUPPORT 93
#endif
#ifndef ENOTSOCK
#define ENOTSOCK 88
#endif
#ifndef EADDRNOTAVAIL
#define EADDRNOTAVAIL 99
#endif
#ifndef ENETDOWN
#define ENETDOWN 100
#endif
#ifndef ENETUNREACH
#define ENETUNREACH 101
#endif
#ifndef ENETRESET
#define ENETRESET 102
#endif
#ifndef ECONNABORTED
#define ECONNABORTED 103
#endif
#ifndef ENOBUFS
#define ENOBUFS 105
#endif
#ifndef EMSGSIZE
#define EMSGSIZE 90
#endif
#ifndef EHOSTDOWN
#define EHOSTDOWN 112
#endif
#ifndef EHOSTUNREACH
#define EHOSTUNREACH 113
#endif
#ifndef EOVERFLOW
#define EOVERFLOW 75
#endif
#ifndef ENOTSUP
#define ENOTSUP 95
#endif
#ifndef EILSEQ
#define EILSEQ 84
#endif
#ifndef ECANCELED
#define ECANCELED 125
#endif
