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

static unsigned int perm_bits(char perm, unsigned int target, mode_t current,
                              int directory) {
    unsigned int result = 0;

    if (perm == 'r')
        result = 4;
    else if (perm == 'w')
        result = 2;
    else if (perm == 'x')
        result = 1;
    else if (perm == 'X')
        result = directory || (current & 0111) != 0 ? 1 : 0;
    else if (perm == 's')
        return target == 1 ? CU_SETUID : target == 2 ? CU_SETGID : 0;
    else if (perm == 't')
        return (target & 4) != 0 ? CU_STICKY : 0;
    else if (perm == 'u')
        result = (current >> 6) & 7;
    else if (perm == 'g')
        result = (current >> 3) & 7;
    else if (perm == 'o')
        result = current & 7;

    if (target == 1)
        result <<= 6;
    else if (target == 2)
        result <<= 3;

    return result;
}

static int parse_symbolic(const char* value, mode_t current, int directory,
                          mode_t* result) {
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

            while (cursor < clause_end && strchr("rwxXstugo", *cursor) != 0) {
                char perm = *cursor++;

                if (perm == 's' && !explicit_who) {
                    bits |= CU_SETUID | CU_SETGID;

                    continue;
                }

                for (unsigned int target = 1; target <= 4; target <<= 1) {
                    if ((allowed & target) != 0)
                        bits |= perm_bits(perm, target, current, directory);
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
                    clear |= 0700 | CU_SETUID;

                if ((allowed & 2) != 0)
                    clear |= 0070 | CU_SETGID;

                if ((allowed & 4) != 0)
                    clear |= 0007 | CU_STICKY;

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

typedef struct {
    DIR*  directory;
    char* path;
} chmod_frame;

static void close_chmod_frame(chmod_frame* frame) {
    closedir(frame->directory);
    free(frame->path);
}

static int change_node(const char* path, const char* mode_text, int recursive,
                       cu_stack* frames) {
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

    size_t length = strlen(path);

    char* owned = malloc(length + 1);
    if (owned == 0) {
        closedir(directory);

        return status | cu_error("chmod", "out of memory");
    }

    memcpy(owned, path, length + 1);

    chmod_frame frame = {directory, owned};

    if (cu_stack_push(frames, &frame) < 0) {
        closedir(directory);
        free(owned);

        return status | cu_error("chmod", "out of memory");
    }

    return status;
}

static int change_tree(const char* path, const char* mode_text, int recursive) {
    cu_stack frames;

    cu_stack_init(&frames, sizeof(chmod_frame));

    int status = change_node(path, mode_text, recursive, &frames);

    while (frames.count != 0) {
        chmod_frame* frame = (chmod_frame*) cu_stack_top(&frames);

        errno = 0;

        struct dirent* entry = readdir(frame->directory);

        if (entry == 0) {
            if (errno != 0)
                status |= cu_error("chmod", strerror(errno));

            close_chmod_frame(frame);
            cu_stack_pop(&frames);

            continue;
        }

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char* child = cu_child_path(frame->path, entry->d_name);
        if (child == 0) {
            status |= cu_error("chmod", "out of memory");
        }
        else {
            status |= change_node(child, mode_text, recursive, &frames);

            free(child);
        }
    }

    cu_stack_destroy(&frames);

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

        if (change_tree(value, mode, recursive) != 0)
            status = 1;
    }

    if (mode == 0 || operands == 0)
        return cu_usage("chmod", "[-R] MODE FILE...");

    return status;
}
