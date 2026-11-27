#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* 0 when mode is a supported RTLD combination, -1 otherwise. */
int ld_mode_valid(int mode);

#ifdef __cplusplus
}
#endif
