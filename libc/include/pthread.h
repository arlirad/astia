#pragma once

#include <sched.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long pthread_t;
typedef unsigned int  pthread_key_t;
typedef uint64_t      pthread_once_t;

typedef struct pthread_attr_t {
    size_t stack_size;
    int    detachstate;
} pthread_attr_t;

typedef struct pthread_mutexattr_t {
    uint32_t magic;
    int      pshared;
    int      type;
    int      protocol;
    int      prioceiling;
    int      robust;
} pthread_mutexattr_t;

typedef struct pthread_mutex_t {
    uint32_t state;
    uint32_t type;
    pthread_t owner;
    uint32_t recursion;
} pthread_mutex_t;

typedef struct pthread_cond_t {
    uint32_t sequence;
} pthread_cond_t;

typedef struct pthread_condattr_t {
    int pshared;
    int clock;
} pthread_condattr_t;

typedef struct __pthread_cleanup_frame {
    void (*routine)(void*);
    void* arg;
    struct __pthread_cleanup_frame* previous;
} __pthread_cleanup_frame;

#define PTHREAD_MUTEX_INITIALIZER { 0u }
#define PTHREAD_COND_INITIALIZER { 0u }
#define PTHREAD_ONCE_INIT 0ull
#define PTHREAD_PROCESS_PRIVATE 0
#define PTHREAD_PROCESS_SHARED 1
#define PTHREAD_MUTEX_NORMAL 0
#define PTHREAD_MUTEX_RECURSIVE 1
#define PTHREAD_MUTEX_ERRORCHECK 2
#define PTHREAD_MUTEX_DEFAULT PTHREAD_MUTEX_NORMAL
#define PTHREAD_PRIO_NONE 0
#define PTHREAD_PRIO_INHERIT 1
#define PTHREAD_PRIO_PROTECT 2
#define PTHREAD_MUTEX_STALLED 0
#define PTHREAD_MUTEX_ROBUST 1
#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1
#define PTHREAD_KEYS_MAX 128
#define PTHREAD_DESTRUCTOR_ITERATIONS 4

#define PTHREAD_CANCEL_ENABLE 0
#define PTHREAD_CANCEL_DISABLE 1
#define PTHREAD_CANCEL_DEFERRED 0
#define PTHREAD_CANCEL_ASYNCHRONOUS 1
#define PTHREAD_CANCELED ((void*) (uintptr_t) -1)

int       pthread_attr_init(pthread_attr_t* attr);
int       pthread_attr_destroy(pthread_attr_t* attr);
int       pthread_attr_setstacksize(pthread_attr_t* attr, size_t size);
int       pthread_attr_setdetachstate(pthread_attr_t* attr, int detachstate);
int       pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                         void* (*start_routine)(void*), void*     argument);
int       pthread_join(pthread_t thread, void** result);
int       pthread_detach(pthread_t thread);
pthread_t pthread_self(void);
int       pthread_equal(pthread_t first, pthread_t second);
int       pthread_key_create(pthread_key_t* key, void (*destructor)(void*));
int       pthread_key_delete(pthread_key_t key);
int       pthread_setspecific(pthread_key_t key, const void* value);
void*     pthread_getspecific(pthread_key_t key);
int       pthread_once(pthread_once_t* once_control, void (*init_routine)(void));
void      pthread_exit(void* result) __attribute__((noreturn));
int       pthread_kill(pthread_t thread, int signal_number);
int       pthread_sigmask(int how, const sigset_t* set, sigset_t* old_set);
int       pthread_setschedparam(pthread_t thread, int policy,
                                const struct sched_param* param);
int       pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr);
int       pthread_mutex_destroy(pthread_mutex_t* mutex);
int       pthread_mutex_lock(pthread_mutex_t* mutex);
int       pthread_mutex_trylock(pthread_mutex_t* mutex);
int       pthread_mutex_unlock(pthread_mutex_t* mutex);
int       pthread_mutex_timedlock(pthread_mutex_t* mutex,
                                  const struct timespec* abstime);
int       pthread_mutexattr_init(pthread_mutexattr_t* attr);
int       pthread_mutexattr_destroy(pthread_mutexattr_t* attr);
int       pthread_mutexattr_getpshared(const pthread_mutexattr_t* attr, int* pshared);
int       pthread_mutexattr_setpshared(pthread_mutexattr_t* attr, int pshared);
int       pthread_mutexattr_gettype(const pthread_mutexattr_t* attr, int* type);
int       pthread_mutexattr_settype(pthread_mutexattr_t* attr, int type);
int       pthread_mutexattr_getprotocol(const pthread_mutexattr_t* attr, int* protocol);
int       pthread_mutexattr_setprotocol(pthread_mutexattr_t* attr, int protocol);
int       pthread_mutexattr_getprioceiling(const pthread_mutexattr_t* attr,
                                           int* prioceiling);
int       pthread_mutexattr_setprioceiling(pthread_mutexattr_t* attr, int prioceiling);
int       pthread_mutexattr_getrobust(const pthread_mutexattr_t* attr, int* robust);
int       pthread_mutexattr_setrobust(pthread_mutexattr_t* attr, int robust);
int       pthread_mutex_getprioceiling(const pthread_mutex_t* mutex, int* prioceiling);
int       pthread_mutex_setprioceiling(pthread_mutex_t* mutex, int prioceiling,
                                       int* old_ceiling);
int       pthread_condattr_init(pthread_condattr_t* attr);
int       pthread_condattr_destroy(pthread_condattr_t* attr);
int       pthread_condattr_getpshared(const pthread_condattr_t* attr, int* pshared);
int       pthread_condattr_setpshared(pthread_condattr_t* attr, int pshared);
int       pthread_condattr_getclock(const pthread_condattr_t* attr, clockid_t* clock_id);
int       pthread_condattr_setclock(pthread_condattr_t* attr, clockid_t clock_id);
int       pthread_cond_init(pthread_cond_t* cond, const pthread_condattr_t* attr);
int       pthread_cond_destroy(pthread_cond_t* cond);
int       pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex);
int       pthread_cond_timedwait(pthread_cond_t* cond, pthread_mutex_t* mutex,
                                 const struct timespec* abstime);
int       pthread_cond_signal(pthread_cond_t* cond);
int       pthread_cond_broadcast(pthread_cond_t* cond);
int       pthread_setcancelstate(int state, int* oldstate);
int       pthread_setcanceltype(int type, int* oldtype);
int       pthread_cancel(pthread_t thread);
void      pthread_testcancel(void);
int       __pthread_cancel_is_enabled(void);
int       __pthread_cancel_pending(void);
void      __pthread_cleanup_push(__pthread_cleanup_frame* frame,
                                 void (*routine)(void*), void* arg);
void      __pthread_cleanup_pop(__pthread_cleanup_frame* frame, int execute);

#define pthread_cleanup_push(routine, arg)                                      \
    do {                                                                        \
        __pthread_cleanup_frame __pthread_cleanup_frame_instance;               \
        __pthread_cleanup_push(&__pthread_cleanup_frame_instance, (routine),   \
                               (arg));
#define pthread_cleanup_pop(execute)                                            \
        __pthread_cleanup_pop(&__pthread_cleanup_frame_instance, (execute));     \
    } while (0)

#ifdef __cplusplus
}
#endif
