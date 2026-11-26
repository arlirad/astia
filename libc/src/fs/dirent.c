#include "internal/file_port.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

struct astia_dir {
    char*         path;
    unsigned long index;
    struct dirent entry;
};

DIR* opendir(const char* path) {
    if (path == 0) {
        errno = EINVAL;

        return 0;
    }

    struct astia_dir* directory = malloc(sizeof *directory);
    if (directory == 0) {
        errno = ENOMEM;

        return 0;
    }

    directory->path = strdup(path);

    if (directory->path == 0) {
        free(directory);

        errno = ENOMEM;

        return 0;
    }

    directory->index = 0;

    return directory;
}

struct dirent* readdir(DIR* handle) {
    struct astia_dir* directory = (struct astia_dir*) handle;
    astia_dirent      result;

    long rc;

    if (directory == 0) {
        errno = EBADF;

        return 0;
    }

    rc = __astia_fio_readdir(directory->path, directory->index, &result);
    if (rc == 0)
        return 0;

    if (rc < 0) {
        errno = (int) -rc;

        return 0;
    }

    ++directory->index;

    directory->entry.d_ino    = directory->index;
    directory->entry.d_type   = (unsigned char) result.type;
    directory->entry.d_reclen = (unsigned short) (result.namelen + 1);

    memcpy(directory->entry.d_name, result.name, result.namelen);

    directory->entry.d_name[result.namelen] = '\0';

    return &directory->entry;
}

int closedir(DIR* handle) {
    struct astia_dir* directory = (struct astia_dir*) handle;
    if (directory == 0) {
        errno = EBADF;

        return -1;
    }

    free(directory->path);
    free(directory);

    return 0;
}

void rewinddir(DIR* handle) {
    struct astia_dir* directory = (struct astia_dir*) handle;
    if (directory != 0)
        directory->index = 0;
}
