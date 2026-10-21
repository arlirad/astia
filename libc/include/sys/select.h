#pragma once

#include <sys/types.h>

typedef struct {
    unsigned long bits[16];
} fd_set;

#define FD_SETSIZE 1024
#define FD_ZERO(set) __builtin_memset((set), 0, sizeof(fd_set))
#define FD_SET(fd, set)                                            \
    ((set)->bits[(unsigned) (fd) / (8 * sizeof(unsigned long))] |= \
     1ul << ((unsigned) (fd) % (8 * sizeof(unsigned long))))
#define FD_CLR(fd, set)                                            \
    ((set)->bits[(unsigned) (fd) / (8 * sizeof(unsigned long))] &= \
     ~(1ul << ((unsigned) (fd) % (8 * sizeof(unsigned long)))))
#define FD_ISSET(fd, set)                                          \
    (((set)->bits[(unsigned) (fd) / (8 * sizeof(unsigned long))] & \
      (1ul << ((unsigned) (fd) % (8 * sizeof(unsigned long))))) != 0)

struct timeval;
int select(int count, fd_set* read_set, fd_set* write_set, fd_set* except_set,
           struct timeval* timeout);
