#include "internal/file_port.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

struct astia_dir {
    int           fd;
    unsigned long index;
    struct dirent entry;
};

DIR* opendir(const char* path) {
    struct astia_dir* directory;

    long fd;

    if (path == 0) {
        errno = EINVAL;

        return 0;
    }

    fd = __astia_fio_open(path, FIO_READ | FIO_DIRECTORY, 0);
    if (fd < 0) {
        errno = (int) -fd;

        return 0;
    }

    directory = malloc(sizeof *directory);
    if (directory == 0) {
        (void) __astia_fio_close((int) fd);

        errno = ENOMEM;

        return 0;
    }

    directory->fd    = (int) fd;
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

    rc = __astia_fio_readdir(directory->fd, directory->index, &result);
    if (rc == 0)
        return 0;

    if (rc < 0) {
        errno = (int) -rc;

        return 0;
    }

    ++directory->index;

    directory->entry.d_ino    = (unsigned long) result.ino;
    directory->entry.d_type   = (unsigned char) result.type;
    directory->entry.d_reclen = (unsigned short) (result.namelen + 1);

    memcpy(directory->entry.d_name, result.name, result.namelen);

    directory->entry.d_name[result.namelen] = '\0';

    return &directory->entry;
}

int closedir(DIR* handle) {
    struct astia_dir* directory = (struct astia_dir*) handle;

    long rc;

    if (directory == 0) {
        errno = EBADF;

        return -1;
    }

    rc = __astia_fio_close(directory->fd);

    free(directory);

    if (rc < 0) {
        errno = (int) -rc;

        return -1;
    }

    return 0;
}

void rewinddir(DIR* handle) {
    struct astia_dir* directory = (struct astia_dir*) handle;
    if (directory != 0)
        directory->index = 0;
}
