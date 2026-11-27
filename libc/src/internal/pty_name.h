#pragma once

#include <errno.h>
#include <stddef.h>
#include <string.h>

/* Copy a NUL-terminated pty name into `dest` only when it fits in
 * `dest_size` bytes.  Returns 0 on success, ERANGE when the name does not
 * fit, and EINVAL for a missing or empty buffer.  `dest` is left untouched
 * unless the copy succeeds. */
static inline int __astia_pty_copy_name(char* dest, size_t dest_size,
                                        const char* source) {
    size_t length = 0;

    if (dest == NULL || source == NULL || dest_size == 0)
        return EINVAL;

    while (source[length] != '\0') {
        if (length + 1 >= dest_size)
            return ERANGE;

        ++length;
    }

    memcpy(dest, source, length + 1);

    return 0;
}
