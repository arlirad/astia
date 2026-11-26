#ifndef ASTIA_LIBC_SYS_RESOURCE_H
#define ASTIA_LIBC_SYS_RESOURCE_H

#include <stdint.h>

#define RLIMIT_CPU 0
#define RLIMIT_FSIZE 1
#define RLIMIT_DATA 2
#define RLIMIT_STACK 3
#define RLIMIT_CORE 4
#define RLIMIT_NOFILE 7
#define RLIMIT_AS 9
#define RLIM_NLIMITS 10
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
