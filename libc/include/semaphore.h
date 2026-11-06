#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t value;
    uint32_t initialized;
} sem_t;

#define SEM_VALUE_MAX 0x7fffffffU

int sem_init(sem_t* sem, int pshared, unsigned int value);
int sem_destroy(sem_t* sem);
int sem_wait(sem_t* sem);
int sem_trywait(sem_t* sem);
int sem_post(sem_t* sem);
int sem_getvalue(const sem_t* sem, int* value);

#ifdef __cplusplus
}
#endif
