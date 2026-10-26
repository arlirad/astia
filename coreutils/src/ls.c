#include "common.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>

static void mode_string(mode_t mode, char* result) {
    result[0] = S_ISDIR(mode) ? 'd' : S_ISLNK(mode) ? 'l' : S_ISREG(mode) ? '-' : '?';
    const int bits[] = {S_IRUSR, S_IWUSR, S_IXUSR, S_IRGRP, S_IWGRP, S_IXGRP,
                        S_IROTH, S_IWOTH, S_IXOTH};
    const char marks[] = "rwxrwxrwx";
    for (int i = 0; i < 9; ++i)
        result[i + 1] = (mode & bits[i]) ? marks[i] : '-';
    result[10] = '\0';
}

static char* join_path(const char* parent, const char* name) {
    size_t a = strlen(parent), b = strlen(name);
    char* result = malloc(a + b + 2);
    if (result == 0) return 0;
    memcpy(result, parent, a);
    result[a] = (a != 0 && parent[a - 1] == '/') ? '\0' : '/';
    if (result[a] == '/') ++a;
    memcpy(result + a, name, b + 1);
    return result;
}

static int print_entry(const char* path, const char* name, int detailed) {
    if (!detailed)
        return cu_puts(name) < 0 || cu_puts("\n") < 0;
    struct stat info;
    if (lstat(path, &info) < 0) return cu_error("ls", strerror(errno));
    char mode[11];
    mode_string(info.st_mode, mode);
    char line[256];
    int length = snprintf(line, sizeof line, "%s %lu %u %u %ld %ld %s",
                          mode, info.st_nlink, info.st_uid, info.st_gid,
                          (long)info.st_size, (long)info.st_mtime, name);
    if (length < 0 || (size_t)length >= sizeof line || cu_write_all(CU_STDOUT, line, (size_t)length) < 0)
        return 1;
    if (S_ISLNK(info.st_mode)) {
        char target[256];
        long n = readlink(path, target, sizeof target - 1);
        if (n < 0) return cu_error("ls", strerror(errno));
        target[n] = '\0';
        if (cu_puts(" -> ") < 0 || cu_puts(target) < 0) return 1;
    }
    return cu_puts("\n") < 0;
}

static int list_one(const char* path, int header, int detailed) {
    struct stat single;
    if (lstat(path, &single) == 0 && !S_ISDIR(single.st_mode)) {
        const char* slash = strrchr(path, '/');
        return print_entry(path, slash ? slash + 1 : path, detailed);
    }
    DIR* directory = opendir(path);
    if (directory == 0)
        return cu_error("ls", strerror(errno));
    if (header && (cu_puts(path) < 0 || cu_puts(":\n") < 0)) {
        closedir(directory);
        return 1;
    }
    struct dirent* entry;
    int            status = 0;
    errno                 = 0;
    while ((entry = readdir(directory)) != 0) {
        char* child = join_path(path, entry->d_name);
        if (child == 0 || print_entry(child, entry->d_name, detailed) != 0) {
            free(child);
            status = 1;
            break;
        }
        free(child);
    }
    if (errno != 0)
        status = cu_error("ls", strerror(errno));
    closedir(directory);
    return status;
}

int main(int argc, char** argv) {
    cu_args args;
    cu_args_init(&args, argc, argv);
    int status  = 0;
    int headers = 0;
    int detailed = 0;
    int operands = 0;
    const char* path;
    if (argc == 1)
        return list_one(".", 0, 0);
    while ((path = cu_args_next(&args)) != 0) {
        if (cu_is_help(path))
            return cu_help("ls", "[OPTION]... [FILE]...", "List directory contents.  -a and -l are supported.");
        if (cu_is_version(path))
            return cu_version("ls");
        if (cu_arg_is(path, "-a", "--all"))
            continue;
        if (cu_arg_is(path, "-l", "--long")) {
            detailed = 1;
            continue;
        }
        if (path[0] == '-')
            return cu_error("ls", "unknown option");
        headers = 1;
        ++operands;
        status |= list_one(path, headers, detailed);
    }
    if (operands == 0)
        return list_one(".", 0, detailed);
    return status;
}
