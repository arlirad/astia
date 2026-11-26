#pragma once

enum { astia_path_buffer_capacity = 256 };

int __astia_resolve_path(const char* path, char* out, unsigned long capacity);
