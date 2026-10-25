#include "common.h"

#include <dirent.h>
#include <errno.h>
#include <string.h>

static int list_one(const char* path, int header) {
    DIR* directory = opendir(path);
    if (directory == 0)
        return cu_error("ls", path);
    if (header && (cu_puts(path) < 0 || cu_puts(":\n") < 0)) {
        closedir(directory);
        return 1;
    }
    struct dirent* entry;
    int            status = 0;
    errno                 = 0;
    while ((entry = readdir(directory)) != 0) {
        if (cu_puts(entry->d_name) < 0 || cu_puts("\n") < 0) {
            status = 1;
            break;
        }
    }
    if (errno != 0)
        status = cu_error("ls", strerror(errno));
    closedir(directory);
    return status;
}

int main(int argc, char** argv) {
    int status  = 0;
    int headers = argc > 2;
    if (argc == 1)
        return list_one(".", 0);
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-' && argv[i][1] == 'a' && argv[i][2] == '\0')
            continue;
        status |= list_one(argv[i], headers);
    }
    return status;
}
