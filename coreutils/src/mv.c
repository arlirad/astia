#include "common.h"
#include <sys/stat.h>

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

static char* child_path(const char* parent, const char* name) {
    size_t parent_len = strlen(parent);
    size_t name_len   = strlen(name);
    int    slash      = parent_len != 0 && parent[parent_len - 1] != '/';

    char* result = malloc(parent_len + (size_t) slash + name_len + 1);
    if (result == 0)
        return 0;

    memcpy(result, parent, parent_len);

    if (slash)
        result[parent_len++] = '/';

    memcpy(result + parent_len, name, name_len + 1);

    return result;
}

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

static int copy_file(const char* source, const char* destination) {
    int input = open(source, O_RDONLY);
    if (input < 0)
        return cu_error("mv", source);

    int output = open(destination, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (output < 0) {
        /* The destination failure is the primary error; the doomed input
         * close is legitimately ignorable. */
        close(input);

        return cu_error("mv", destination);
    }

    char buffer[4096];
    int  status = 0;

    for (;;) {
        long count = read(input, buffer, sizeof buffer);
        if (count == 0)
            break;

        if (count < 0 || cu_write_all(output, buffer, (size_t) count) < 0) {
            status = cu_error("mv", source);

            break;
        }
    }

    if (cu_close(input, "mv", source) != 0)
        status = 1;

    if (cu_close(output, "mv", destination) != 0)
        status = 1;

    return status;
}

static int move_file(const char* source, const char* destination) {
    struct stat destination_info;

    const char* target = destination;
    char*       joined = 0;

    /* An existing directory destination means "move inside it", mirroring
       cp, rather than handing the directory itself to rename. */
    if (stat(destination, &destination_info) == 0 && S_ISDIR(destination_info.st_mode)) {
        joined = child_path(destination, base_name(source));
        if (joined == 0)
            return cu_error("mv", "out of memory");

        target = joined;
    }

    int status = 0;

    if (rename(source, target) != 0) {
        struct stat source_info;

        if (errno != EXDEV) {
            status = cu_error("mv", source);
        }
        else if (stat(source, &source_info) == 0 && S_ISDIR(source_info.st_mode)) {
            /* Cross-filesystem moves cannot be atomic, and copy+unlink of a
               tree does not belong in mv; refuse instead of half-moving it. */
            status = cu_error("mv", "cannot move a directory across filesystems");
        }
        else {
            status = copy_file(source, target);

            if (status == 0 && unlink(source) < 0)
                status = cu_error("mv", source);
        }
    }

    free(joined);

    return status;
}

int main(int argc, char** argv) {
    cu_args args;

    cu_args_init(&args, argc, argv);

    const char* source      = 0;
    const char* destination = 0;
    const char* value;

    while ((value = cu_args_next(&args)) != 0) {
        if (cu_is_help(value))
            return cu_help("mv", "SOURCE DEST",
                           "Move SOURCE to DEST, or into DEST if it is a directory.");

        if (cu_is_version(value))
            return cu_version("mv");

        if (args.options && value[0] == '-')
            return cu_error("mv", "unknown option");

        if (source == 0)
            source = value;
        else if (destination == 0)
            destination = value;
        else
            return cu_usage("mv", "SOURCE DEST");
    }

    if (source == 0 || destination == 0)
        return cu_usage("mv", "SOURCE DEST");

    return move_file(source, destination);
}
