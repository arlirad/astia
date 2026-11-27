#include "common.h"
#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>

typedef enum { follow_never, follow_command_line, follow_always } follow_mode;

typedef struct {
    DIR*   directory;
    char*  path;
    char*  resolved;
    size_t depth;
} find_frame;

static int loop_error(const char* path) {
    cu_puts_fd(CU_STDERR, "find: filesystem loop detected: ");
    cu_puts_fd(CU_STDERR, path);
    cu_puts_fd(CU_STDERR, "\n");

    return 1;
}

static int is_ancestor(const cu_stack* frames, const char* resolved) {
    const find_frame* items = (const find_frame*) frames->items;

    for (size_t index = 0; index < frames->count; ++index) {
        if (items[index].resolved != 0 &&
            __builtin_strcmp(items[index].resolved, resolved) == 0)
            return 1;
    }

    return 0;
}

static void close_find_frame(find_frame* frame) {
    closedir(frame->directory);
    free(frame->path);
    free(frame->resolved);
}

static int enter_path(const char* path, follow_mode follow, size_t depth,
                      cu_stack* frames) {
    struct stat info;

    int follow_path =
        follow == follow_always || (follow == follow_command_line && depth == 0);

    if ((follow_path ? stat(path, &info) : lstat(path, &info)) < 0 || cu_puts(path) < 0 ||
        cu_puts("\n") < 0)
        return cu_error("find", path);

    if (!S_ISDIR(info.st_mode))
        return 0;

    char* resolved = 0;

    if (follow == follow_always) {
        resolved = realpath(path, 0);
        if (resolved == 0)
            return cu_error("find", path);

        if (is_ancestor(frames, resolved)) {
            free(resolved);

            return loop_error(path);
        }
    }

    size_t length = __builtin_strlen(path);

    char* owned = malloc(length + 1);
    if (owned == 0) {
        free(resolved);

        return cu_error("find", "out of memory");
    }

    __builtin_memcpy(owned, path, length + 1);

    DIR* directory = opendir(path);
    if (directory == 0) {
        free(owned);
        free(resolved);

        return cu_error("find", path);
    }

    find_frame frame = {directory, owned, resolved, depth};

    if (cu_stack_push(frames, &frame) < 0) {
        closedir(directory);
        free(owned);
        free(resolved);

        return cu_error("find", "out of memory");
    }

    return 0;
}

static int find_walk(const char* path, follow_mode follow) {
    cu_stack frames;

    cu_stack_init(&frames, sizeof(find_frame));

    int status = enter_path(path, follow, 0, &frames);

    while (frames.count != 0) {
        find_frame* frame = (find_frame*) cu_stack_top(&frames);

        errno = 0;

        struct dirent* entry = readdir(frame->directory);

        if (entry == 0) {
            if (errno != 0)
                status = cu_error("find", frame->path);

            close_find_frame(frame);
            cu_stack_pop(&frames);

            continue;
        }

        if (__builtin_strcmp(entry->d_name, ".") == 0 ||
            __builtin_strcmp(entry->d_name, "..") == 0)
            continue;

        char* child = cu_child_path(frame->path, entry->d_name);
        if (child == 0) {
            status = cu_error("find", "out of memory");

            break;
        }

        size_t child_depth = frame->depth + 1;

        if (enter_path(child, follow, child_depth, &frames) != 0)
            status = 1;

        free(child);
    }

    while (frames.count != 0) {
        find_frame* frame = (find_frame*) cu_stack_top(&frames);

        close_find_frame(frame);
        cu_stack_pop(&frames);
    }

    cu_stack_destroy(&frames);

    return status;
}

int main(int argc, char** argv) {
    cu_args     args;
    const char* path;
    follow_mode follow = follow_never;

    int operands = 0;
    int status   = 0;

    cu_args_init(&args, argc, argv);

    while ((path = cu_args_next(&args)) != 0) {
        if (cu_is_help(path))
            return cu_help("find", "[-H] [-L] [-P] [PATH...]",
                           "Print each PATH and its descendants.\n"
                           "\n"
                           "  -H  follow command-line symbolic links\n"
                           "  -L  follow all symbolic links\n"
                           "  -P  never follow symbolic links (default)");

        if (cu_is_version(path))
            return cu_version("find");

        if (args.options && path[0] == '-') {
            if (__builtin_strcmp(path, "-H") == 0)
                follow = follow_command_line;
            else if (__builtin_strcmp(path, "-L") == 0)
                follow = follow_always;
            else if (__builtin_strcmp(path, "-P") == 0)
                follow = follow_never;
            else
                return cu_error("find", "unknown option");

            continue;
        }

        ++operands;

        if (find_walk(path, follow) != 0)
            status = 1;
    }

    if (operands == 0)
        status = find_walk(".", follow);

    return operands == 0 && status == 0 ? 0 : status;
}
