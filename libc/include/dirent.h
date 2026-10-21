#pragma once

#include <sys/types.h>

typedef struct astia_dir DIR;

struct dirent {
    unsigned long  d_ino;
    unsigned short d_reclen;
    unsigned char  d_type;
    char           d_name[256];
};

#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_REG 8
#define DT_LNK 10

DIR*           opendir(const char* path);
struct dirent* readdir(DIR* directory);
int            closedir(DIR* directory);
void           rewinddir(DIR* directory);
