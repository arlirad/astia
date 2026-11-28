#pragma once

#include <sys/stat.h>
#include <sys/types.h>

#ifndef S_ISUID
#define S_ISUID 04000
#endif

#ifndef S_ISGID
#define S_ISGID 02000
#endif

#ifndef S_ISVTX
#define S_ISVTX 01000
#endif

static inline char cu_ls_type_character(mode_t mode) {
    if (S_ISREG(mode))
        return '-';

    if (S_ISDIR(mode))
        return 'd';

    if (S_ISLNK(mode))
        return 'l';

    if (S_ISCHR(mode))
        return 'c';

    if (S_ISBLK(mode))
        return 'b';

    if (S_ISFIFO(mode))
        return 'p';

    if (S_ISSOCK(mode))
        return 's';

    return '?';
}

static inline void cu_ls_mode_string(mode_t mode, char* result) {
    result[0] = cu_ls_type_character(mode);

    const mode_t bits[] = {S_IRUSR, S_IWUSR, S_IXUSR, S_IRGRP, S_IWGRP,
                           S_IXGRP, S_IROTH, S_IWOTH, S_IXOTH};

    const char marks[] = "rwxrwxrwx";

    for (int index = 0; index < 9; ++index) {
        result[index + 1] = (mode & bits[index]) ? marks[index] : '-';
    }

    if (mode & S_ISUID)
        result[3] = (mode & S_IXUSR) ? 's' : 'S';

    if (mode & S_ISGID)
        result[6] = (mode & S_IXGRP) ? 's' : 'S';

    if (mode & S_ISVTX)
        result[9] = (mode & S_IXOTH) ? 't' : 'T';

    result[10] = '\0';
}
