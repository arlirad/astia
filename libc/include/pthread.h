#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long pthread_t;

typedef struct pthread_attr_t {
    size_t stack_size;
} pthread_attr_t;

int       pthread_attr_init(pthread_attr_t* attr);
int       pthread_attr_destroy(pthread_attr_t* attr);
int       pthread_attr_setstacksize(pthread_attr_t* attr, size_t size);
int       pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                         void* (*start_routine)(void*), void*     argument);
int       pthread_join(pthread_t thread, void** result);
int       pthread_detach(pthread_t thread);
pthread_t pthread_self(void);
void      pthread_exit(void* result) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif
