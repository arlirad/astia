#include "common.h"
#include "read_retry.h"
#include <sys/stat.h>

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    DIR*  directory;
    char* source;
    char* destination;
} cp_frame;

static const char* base_name(const char* path) {
    const char* end = path + strlen(path);

    while (end != path && end[-1] == '/') {
        --end;
    }

    while (end != path && end[-1] != '/') {
        --end;
    }

    return end;
}

static int prepare_output(int input, int output) {
    struct stat input_info;
    struct stat output_info;

    int input_known  = fstat(input, &input_info) == 0;
    int output_known = fstat(output, &output_info) == 0;

    if (input_known && output_known && input_info.st_dev == output_info.st_dev &&
        input_info.st_ino == output_info.st_ino)
        return cu_error("cp", "source and destination are the same file");

    if (output_known && !S_ISREG(output_info.st_mode))
        return 0;

    if (ftruncate(output, 0) < 0)
        return cu_error("cp", "cannot truncate destination");

    return 0;
}

static int destination_inside_source(const char* source, const char* destination) {
    char resolved_source[PATH_MAX];
    char resolved_destination[PATH_MAX];

    if (realpath(source, resolved_source) == 0)
        return 0;

    if (realpath(destination, resolved_destination) == 0) {
        const char* name = base_name(destination);

        size_t prefix = (size_t) (name - destination);
        char   parent[PATH_MAX];

        if (prefix == 0) {
            if (realpath(".", resolved_destination) == 0)
                return 0;
        }
        else {
            if (prefix >= sizeof parent)
                return 0;

            memcpy(parent, destination, prefix);

            parent[prefix] = '\0';

            if (realpath(parent, resolved_destination) == 0)
                return 0;
        }

        size_t length      = strlen(resolved_destination);
        size_t name_length = strlen(name);

        if (length + name_length + 2 > sizeof resolved_destination)
            return 0;

        if (length == 0 || resolved_destination[length - 1] != '/')
            resolved_destination[length++] = '/';

        memcpy(resolved_destination + length, name, name_length + 1);
    }

    size_t source_length = strlen(resolved_source);

    while (source_length > 1 && resolved_source[source_length - 1] == '/') {
        --source_length;
    }

    if (__builtin_strncmp(resolved_source, resolved_destination, source_length) != 0)
        return 0;

    return source_length == 1 || resolved_destination[source_length] == '/';
}

static int copy_fd(int input, int output, const char* name) {
    char buffer[4096];

    for (;;) {
        long count = cu_read_retry(input, buffer, sizeof buffer);
        if (count == 0)
            return 0;

        if (count < 0 || cu_write_all(output, buffer, (size_t) count) < 0)
            return cu_error("cp", name);
    }
}

static int copy_node(const char* source, const char* destination, cu_stack* frames);

static void close_cp_frame(cp_frame* frame) {
    closedir(frame->directory);
    free(frame->source);
    free(frame->destination);
}

static int copy_directory_node(const char* source, const char* destination, mode_t mode,
                               cu_stack* frames) {
    if (destination_inside_source(source, destination))
        return cu_error("cp", "cannot copy a directory into itself");

    if (mkdir(destination, mode & 0777) < 0) {
        struct stat existing;

        if (errno != EEXIST || stat(destination, &existing) < 0 ||
            !S_ISDIR(existing.st_mode))
            return cu_error("cp", destination);
    }

    DIR* directory = opendir(source);
    if (directory == 0)
        return cu_error("cp", source);

    size_t source_length      = strlen(source);
    size_t destination_length = strlen(destination);

    char* owned_source      = malloc(source_length + 1);
    char* owned_destination = malloc(destination_length + 1);

    if (owned_source == 0 || owned_destination == 0) {
        closedir(directory);
        free(owned_source);
        free(owned_destination);

        return cu_error("cp", "out of memory");
    }

    memcpy(owned_source, source, source_length + 1);
    memcpy(owned_destination, destination, destination_length + 1);

    cp_frame frame = {directory, owned_source, owned_destination};

    if (cu_stack_push(frames, &frame) < 0) {
        closedir(directory);
        free(owned_source);
        free(owned_destination);

        return cu_error("cp", "out of memory");
    }

    return 0;
}

static int copy_node(const char* source, const char* destination, cu_stack* frames) {
    struct stat info;

    int input;
    int output;
    int status;

    if (stat(source, &info) < 0)
        return cu_error("cp", source);

    if (S_ISDIR(info.st_mode))
        return copy_directory_node(source, destination, info.st_mode, frames);

    input = open(source, O_RDONLY);
    if (input < 0)
        return cu_error("cp", source);

    output = open(destination, O_WRONLY | O_CREAT, info.st_mode & 0777);
    if (output < 0) {
        /* The destination failure is the primary error; the doomed input
         * close is legitimately ignorable. */
        close(input);

        return cu_error("cp", destination);
    }

    status = prepare_output(input, output);
    if (status == 0)
        status = copy_fd(input, output, source);

    if (cu_close(input, "cp", source) != 0)
        status = 1;

    if (cu_close(output, "cp", destination) != 0)
        status = 1;

    return status;
}

static int copy_tree(const char* source, const char* destination) {
    cu_stack frames;

    cu_stack_init(&frames, sizeof(cp_frame));

    int status = copy_node(source, destination, &frames);

    while (frames.count != 0) {
        cp_frame* frame = (cp_frame*) cu_stack_top(&frames);

        errno = 0;

        struct dirent* entry = readdir(frame->directory);

        if (entry == 0) {
            if (errno != 0)
                status = cu_error("cp", frame->source);

            close_cp_frame(frame);
            cu_stack_pop(&frames);

            continue;
        }

        if (__builtin_strcmp(entry->d_name, ".") == 0 ||
            __builtin_strcmp(entry->d_name, "..") == 0)
            continue;

        char* from = cu_child_path(frame->source, entry->d_name);
        char* to   = cu_child_path(frame->destination, entry->d_name);

        if (from == 0 || to == 0) {
            free(from);
            free(to);

            status = cu_error("cp", "out of memory");

            break;
        }

        status |= copy_node(from, to, &frames);

        free(from);
        free(to);
    }

    while (frames.count != 0) {
        cp_frame* frame = (cp_frame*) cu_stack_top(&frames);

        close_cp_frame(frame);
        cu_stack_pop(&frames);
    }

    cu_stack_destroy(&frames);

    return status;
}

int main(int argc, char** argv) {
    cu_args args;

    cu_args_init(&args, argc, argv);

    const char* source      = 0;
    const char* destination = 0;

    int recursive = 0;

    const char* value;

    while ((value = cu_args_next(&args)) != 0) {
        if (cu_is_help(value))
            return cu_help(
                "cp", "[-r] SOURCE DEST",
                "Copy files or directories; - denotes standard input or output.");

        if (cu_is_version(value))
            return cu_version("cp");

        if (args.options && cu_arg_is(value, "-r", "--recursive")) {
            recursive = 1;

            continue;
        }

        if (args.options && value[0] == '-' && __builtin_strcmp(value, "-") != 0)
            return cu_error("cp", "unknown option");

        if (source == 0)
            source = value;
        else if (destination == 0)
            destination = value;
        else
            return cu_usage("cp", "SOURCE DEST");
    }

    if (source == 0 || destination == 0)
        return cu_usage("cp", "[-r] SOURCE DEST");

    struct stat source_info;

    int source_is_stdin = __builtin_strcmp(source, "-") == 0;

    if (!source_is_stdin && stat(source, &source_info) < 0)
        return cu_error("cp", source);

    if (!source_is_stdin && S_ISDIR(source_info.st_mode) && !recursive)
        return cu_error("cp", "omitting directory (use -r)");

    char*       target      = 0;
    const char* target_path = destination;
    struct stat destination_info;

    if (stat(destination, &destination_info) == 0 && S_ISDIR(destination_info.st_mode)) {
        target = cu_child_path(destination, base_name(source));
        if (target == 0)
            return cu_error("cp", "out of memory");

        target_path = target;
    }

    if (__builtin_strcmp(source, "-") == 0 || __builtin_strcmp(destination, "-") == 0) {
        int input  = CU_STDIN;
        int output = CU_STDOUT;

        if (__builtin_strcmp(source, "-") != 0)
            input = open(source, O_RDONLY);

        if (__builtin_strcmp(destination, "-") != 0)
            output = open(destination, O_WRONLY | O_CREAT, 0666);

        if (input < 0 || output < 0) {
            /* The failed open is the primary error; closing whichever
             * descriptor did open is legitimately ignorable. */
            if (input > CU_STDERR)
                close(input);

            if (output > CU_STDERR)
                close(output);

            free(target);

            return cu_error("cp", destination);
        }

        int status = 0;

        if (output > CU_STDERR)
            status = prepare_output(input, output);

        if (status == 0)
            status = copy_fd(input, output, source);

        if (input > CU_STDERR && cu_close(input, "cp", source) != 0)
            status = 1;

        if (output > CU_STDERR && cu_close(output, "cp", destination) != 0)
            status = 1;

        free(target);

        return status;
    }

    int status = copy_tree(source, target_path);

    free(target);

    return status;
}
