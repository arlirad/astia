#pragma once

#include <limits.h>

enum { astia_path_buffer_capacity = PATH_MAX };

#pragma GCC visibility push(default)
long                   __astia_getcwd_call(char* buffer, unsigned long length);
long                   __astia_chdir_call(const char* path);
#pragma GCC visibility pop

int __astia_resolve_path(const char* path, char* out, unsigned long capacity);
