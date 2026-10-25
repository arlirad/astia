#include "common.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

static int selected[65];
static int selected_field(unsigned long field) { return field <= 64 && selected[field]; }

static int cut_fd(int fd, const char* name, char delimiter) {
    char line[4096];
    for (;;) {
        size_t length = 0; int ended = 0;
        while (length < sizeof line - 1) {
            long got = read(fd, line + length, 1);
            if (got < 0) return cu_error("cut", name);
            if (got == 0) { ended = 1; break; }
            if (line[length++] == '\n') break;
        }
        if (length == 0 && ended) break;
        line[length] = '\0';
        char* cursor = line;
        unsigned long field = 1;
        int emitted = 0;
        for (;;) {
            char* end = cursor;
            while (*end != delimiter && *end != '\n' && *end != '\0') ++end;
            char saved = *end;
            *end = '\0';
            if (selected_field(field)) {
                if (emitted && cu_write_all(CU_STDOUT, &delimiter, 1) < 0) return 1;
                if (cu_write_all(CU_STDOUT, cursor, strlen(cursor)) < 0) return 1;
                emitted = 1;
            }
            if (saved == '\0' || saved == '\n') break;
            *end = saved; cursor = end + 1; ++field;
        }
        if (cu_puts_fd(CU_STDOUT, "\n") < 0) return 1;
        if (!ended && length == sizeof line - 1) {
            do {
                char c; long got = read(fd, &c, 1);
                if (got < 0) return cu_error("cut", name);
                if (got == 0 || c == '\n') { ended = 1; break; }
            } while (1);
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    cu_args args; cu_args_init(&args, argc, argv);
    char delimiter = '\t'; const char* list = 0; const char* value; int first_file = -1;
    while ((value = cu_args_next(&args)) != 0) {
        if (cu_is_help(value)) return cu_help("cut", "-f LIST [-d DELIM] [FILE]...", "Print selected fields from each input line.");
        if (cu_is_version(value)) return cu_version("cut");
        if (args.options && __builtin_strcmp(value, "-d") == 0) {
            if (args.index >= argc || argv[args.index][0] == '\0') return cu_usage("cut", "-f LIST [-d DELIM] [FILE]...");
            delimiter = argv[args.index++][0];
            continue;
        }
        if (args.options && __builtin_strcmp(value, "-f") == 0) {
            if (args.index >= argc) return cu_usage("cut", "-f LIST [-d DELIM] [FILE]...");
            list = argv[args.index++]; continue;
        }
        if (args.options && value[0] == '-') return cu_error("cut", "unknown option");
        if (list == 0) return cu_usage("cut", "-f LIST [-d DELIM] [FILE]...");
        first_file = args.index - 1;
        break;
    }
    if (list == 0) return cu_usage("cut", "-f LIST [-d DELIM] [FILE]...");
    const char* p = list;
    while (*p != '\0') {
        char* end; unsigned long first = strtoul(p, &end, 10);
        if (end == p || first == 0 || first > 64) return cu_usage("cut", "-f LIST [-d DELIM] [FILE]...");
        unsigned long last = first;
        if (*end == '-') { last = strtoul(end + 1, &end, 10); if (last == 0 || last > 64) return cu_usage("cut", "-f LIST [-d DELIM] [FILE]..."); }
        for (unsigned long i = first; i <= last; ++i) selected[i] = 1;
        if (*end == '\0') break;
        if (*end != ',') return cu_usage("cut", "-f LIST [-d DELIM] [FILE]...");
        p = end + 1;
    }
    int status = 0;
    if (first_file < 0) return cut_fd(CU_STDIN, "standard input", delimiter);
    for (int i = first_file; i < argc; ++i) {
        int fd = __builtin_strcmp(argv[i], "-") == 0 ? CU_STDIN : open(argv[i], O_RDONLY);
        if (fd < 0) { cu_error("cut", argv[i]); status = 1; continue; }
        status |= cut_fd(fd, argv[i], delimiter);
        if (fd != CU_STDIN) close(fd);
    }
    return status;
}
