#include "common.h"
#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CU_SETUID 04000
#define CU_SETGID 02000
#define CU_STICKY 01000
#define CU_PERMS 07777

static int is_octal(const char* value) {
    if (*value == '\0')
        return 0;

    for (const char* cursor = value; *cursor != '\0'; ++cursor) {
        if (*cursor < '0' || *cursor > '7')
            return 0;
    }

    return 1;
}

static int parse_octal(const char* value, mode_t* result) {
    unsigned int mode = 0;

    for (const char* cursor = value; *cursor != '\0'; ++cursor) {
        mode = (mode << 3) | (unsigned int) (*cursor - '0');
        if (mode > CU_PERMS)
            return -1;
    }

    *result = (mode_t) mode;

    return 0;
}

static unsigned int who_bits(char who) {
    if (who == 'u')
        return 1;

    if (who == 'g')
        return 2;

    if (who == 'o')
        return 4;

    return 7;
}

static unsigned int perm_bits(char perm, unsigned int who, mode_t current,
                              mode_t original, int directory) {
    unsigned int result = 0;

    if (perm == 'r')
        result = 4;
    else if (perm == 'w')
        result = 2;
    else if (perm == 'x')
        result = 1;
    else if (perm == 'X')
        result = directory || (original & 0111) != 0 ? 1 : 0;
    else if (perm == 's')
        return who == 1 ? CU_SETUID : who == 2 ? CU_SETGID : 0;
    else if (perm == 't')
        return CU_STICKY;
    else if (perm == 'u')
        result = (current >> 6) & 7;
    else if (perm == 'g')
        result = (current >> 3) & 7;
    else if (perm == 'o')
        result = current & 7;

    if (who & 1)
        result |= (result & 7) << 6;

    if (who & 2)
        result |= (result & 7) << 3;

    if (who & 4)
        result |= result & 7;

    return result;
}

static int parse_symbolic(const char* value, mode_t current, int directory,
                          mode_t* result) {
    mode_t original = current;

    unsigned int mask = umask(0);

    (void) umask(mask);

    const char* cursor = value;

    while (*cursor != '\0') {
        unsigned int who          = 0;
        int          explicit_who = 0;

        while (*cursor == 'u' || *cursor == 'g' || *cursor == 'o' || *cursor == 'a') {
            explicit_who = 1;
            who |= who_bits(*cursor++);
        }

        unsigned int allowed = explicit_who ? who : 7;

        const char* clause_end = cursor;

        while (*clause_end != '\0' && *clause_end != ',') {
            ++clause_end;
        }

        if (*cursor != '+' && *cursor != '-' && *cursor != '=')
            return -1;

        while (*cursor == '+' || *cursor == '-' || *cursor == '=') {
            char operation = *cursor++;

            mode_t bits = 0;

            int has_perm = 0;

            while (cursor < clause_end && strchr("rwxXstugo", *cursor) != 0) {
                char perm = *cursor++;

                has_perm = 1;

                if (perm == 's' && !explicit_who) {
                    bits |= CU_SETUID | CU_SETGID;

                    continue;
                }

                if (perm == 't') {
                    bits |= CU_STICKY;

                    continue;
                }

                if (perm == 'u' || perm == 'g' || perm == 'o') {
                    for (unsigned int target = 1; target <= 4; target <<= 1) {
                        if ((allowed & target) != 0)
                            bits |= perm_bits(perm, target, current, original, directory);
                    }
                }
                else {
                    for (unsigned int target = 1; target <= 4; target <<= 1) {
                        if ((allowed & target) != 0)
                            bits |= perm_bits(perm, target, current, original, directory);
                    }
                }
            }

            if (cursor != clause_end)
                return -1;

            if (!explicit_who)
                bits &= (mode_t) ~(mask & 0777);

            if (operation == '+') {
                current |= bits;
            }
            else if (operation == '-') {
                current &= (mode_t) ~bits;
            }
            else {
                mode_t clear = 0;

                if ((allowed & 1) != 0)
                    clear |= 0700;

                if ((allowed & 2) != 0)
                    clear |= 0070;

                if ((allowed & 4) != 0)
                    clear |= 0007;

                if (has_perm && (bits & CU_SETUID) != 0)
                    clear |= CU_SETUID;

                if (has_perm && (bits & CU_SETGID) != 0)
                    clear |= CU_SETGID;

                if (has_perm && (bits & CU_STICKY) != 0)
                    clear |= CU_STICKY;

                current = (mode_t) ((current & ~clear) | bits);
            }
        }

        if (*clause_end == ',') {
            cursor = clause_end + 1;

            if (*cursor == '\0')
                return -1;
        }
        else {
            cursor = clause_end;
        }

        if (*cursor == '\0')
            break;
    }

    *result = current & CU_PERMS;

    return 0;
}

static int parse_mode(const char* value, mode_t current, int directory, mode_t* result) {
    if (is_octal(value))
        return parse_octal(value, result);

    return parse_symbolic(value, current, directory, result);
}

static char* child_path(const char* parent, const char* name) {
    size_t parent_length = strlen(parent);
    size_t name_length   = strlen(name);
    int    slash         = parent_length != 0 && parent[parent_length - 1] != '/';

    char* result = malloc(parent_length + (size_t) slash + name_length + 1);
    if (result == 0)
        return 0;

    memcpy(result, parent, parent_length);

    if (slash)
        result[parent_length++] = '/';

    memcpy(result + parent_length, name, name_length + 1);

    return result;
}

static int change_one(const char* path, const char* mode_text, int recursive) {
    struct stat info;

    if (lstat(path, &info) < 0)
        return cu_error("chmod", strerror(errno));

    mode_t mode;

    if (parse_mode(mode_text, info.st_mode & CU_PERMS, S_ISDIR(info.st_mode), &mode) != 0)
        return cu_error("chmod", "invalid mode");

    int status = 0;

    if (chmod(path, mode) < 0)
        status = cu_error("chmod", strerror(errno));

    if (!recursive || !S_ISDIR(info.st_mode))
        return status;

    DIR* directory = opendir(path);
    if (directory == 0)
        return status | cu_error("chmod", strerror(errno));

    for (;;) {
        errno = 0;

        struct dirent* entry = readdir(directory);
        if (entry == 0) {
            if (errno != 0)
                status |= cu_error("chmod", strerror(errno));

            break;
        }

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char* child = child_path(path, entry->d_name);
        if (child == 0) {
            status |= cu_error("chmod", "out of memory");
        }
        else {
            status |= change_one(child, mode_text, 1);

            free(child);
        }
    }

    closedir(directory);

    return status;
}

int main(int argc, char** argv) {
    cu_args args;

    cu_args_init(&args, argc, argv);

    int recursive = 0;

    const char* mode = 0;

    int operands = 0;
    int status   = 0;

    const char* value;

    while ((value = cu_args_next(&args)) != 0) {
        if (cu_is_help(value))
            return cu_help("chmod", "[-R] MODE FILE...",
                           "Change file mode bits; -R changes directory trees.");

        if (cu_is_version(value))
            return cu_version("chmod");

        if (args.options && cu_arg_is(value, "-R", "--recursive")) {
            recursive = 1;

            continue;
        }

        if (args.options && value[0] == '-')
            return cu_error("chmod", "unknown option");

        if (mode == 0) {
            mode = value;

            continue;
        }

        ++operands;

        if (change_one(value, mode, recursive) != 0)
            status = 1;
    }

    if (mode == 0 || operands == 0)
        return cu_usage("chmod", "[-R] MODE FILE...");

    return status;
}
