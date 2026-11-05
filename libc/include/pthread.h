#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long pthread_t;

typedef struct pthread_attr_t {
    size_t stack_size;
} pthread_attr_t;

typedef struct pthread_mutexattr_t {
    int unused;
} pthread_mutexattr_t;

typedef struct pthread_mutex_t {
    uint32_t state;
} pthread_mutex_t;

#define PTHREAD_MUTEX_INITIALIZER { 0u }

#define PTHREAD_CANCEL_ENABLE 0
#define PTHREAD_CANCEL_DISABLE 1
#define PTHREAD_CANCEL_DEFERRED 0
#define PTHREAD_CANCEL_ASYNCHRONOUS 1

int       pthread_attr_init(pthread_attr_t* attr);
int       pthread_attr_destroy(pthread_attr_t* attr);
int       pthread_attr_setstacksize(pthread_attr_t* attr, size_t size);
int       pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                         void* (*start_routine)(void*), void*     argument);
int       pthread_join(pthread_t thread, void** result);
int       pthread_detach(pthread_t thread);
pthread_t pthread_self(void);
void      pthread_exit(void* result) __attribute__((noreturn));
int       pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr);
int       pthread_mutex_destroy(pthread_mutex_t* mutex);
int       pthread_mutex_lock(pthread_mutex_t* mutex);
int       pthread_mutex_trylock(pthread_mutex_t* mutex);
int       pthread_mutex_unlock(pthread_mutex_t* mutex);
int       pthread_setcancelstate(int state, int* oldstate);
int       pthread_setcanceltype(int type, int* oldtype);

#ifdef __cplusplus
}
#endif
