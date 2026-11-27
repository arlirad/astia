#include "internal/account_fields.h"

#include <stddef.h>

size_t __astia_account_field_split(char* line, char delimiter, char* fields[],
                                   size_t capacity) {
    char* cursor = line;

    size_t count = 0;

    for (;;) {
        if (count < capacity)
            fields[count] = cursor;

        ++count;

        while (*cursor != '\0' && *cursor != delimiter && *cursor != '\n') {
            ++cursor;
        }

        if (*cursor == '\0')
            break;

        if (*cursor == '\n') {
            *cursor = '\0';

            break;
        }

        *cursor++ = '\0';
    }

    return count;
}
