#ifndef ASTIA_LIBC_SYS_RESOURCE_H
#define ASTIA_LIBC_SYS_RESOURCE_H

#include <stdint.h>

#define RLIMIT_AS 9
#define RLIMIT_NOFILE 7
#define RLIM_INFINITY UINT64_MAX

typedef uint64_t rlim_t;
struct rlimit {
    rlim_t rlim_cur;
    rlim_t rlim_max;
};

#ifdef __cplusplus
extern "C" {
#endif
int getrlimit(int resource, struct rlimit* limit);
int setrlimit(int resource, const struct rlimit* limit);
#ifdef __cplusplus
}
#endif

#endif
