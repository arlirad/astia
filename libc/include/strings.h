#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int   bcmp(const void* left, const void* right, size_t length);
void  bcopy(const void* source, void* destination, size_t length);
void  bzero(void* destination, size_t length);
int   ffs(int value);
char* index(const char* string, int character);
char* rindex(const char* string, int character);
int   strcasecmp(const char* left, const char* right);
int   strncasecmp(const char* left, const char* right, size_t length);

#ifdef __cplusplus
}
#endif
