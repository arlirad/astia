#pragma once

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

clock_t times(struct tms* result);

#ifdef __cplusplus
}
#endif
