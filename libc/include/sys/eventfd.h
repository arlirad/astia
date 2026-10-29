#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define EFD_NONBLOCK 0x0001
#define EFD_CLOEXEC  0x0002

int eventfd(unsigned int initial_value, int flags);

#ifdef __cplusplus
}
#endif
