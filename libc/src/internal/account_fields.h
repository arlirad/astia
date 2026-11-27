#pragma once

#include <stddef.h>

size_t __astia_account_field_split(char* line, char delimiter, char* fields[],
                                   size_t capacity);
