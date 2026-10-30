#include "common.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

static int reverse_order;
static int unique_lines;

static void free_lines(char** lines, size_t count) {
    for (size_t i = 0; i < count; ++i)
        free(lines[i]);
    free(lines);
}

static int compare_lines(const void* left, const void* right) {
    int result = strcmp(*(const char* const*)left, *(const char* const*)right);
    return reverse_order ? -result : result;
}

static int sort_fd(int fd, const char* name, char*** lines, size_t* count, size_t* capacity) {
    char buffer[4096];
    for (;;) {
        size_t length = 0;
        int ended = 0;
        while (length < sizeof buffer - 1) {
            long got = read(fd, buffer + length, 1);
            if (got < 0) return cu_error("sort", name);
            if (got == 0) { ended = 1; break; }
            if (buffer[length++] == '\n') break;
        }
        if (length == 0 && ended) break;
        buffer[length] = '\0';
        char* line = (char*)malloc(length + 1);
        if (line == 0) return cu_error("sort", "out of memory");
        memcpy(line, buffer, length + 1);
        if (*count == *capacity) {
            size_t next = *capacity == 0 ? 16 : *capacity * 2;
            char** grown = (char**)realloc(*lines, next * sizeof(char*));
            if (grown == 0) { free(line); return cu_error("sort", "out of memory"); }
            *lines = grown; *capacity = next;
        }
        (*lines)[(*count)++] = line;
        if (!ended && length == sizeof buffer - 1) {
            do {
                char c;
                long got = read(fd, &c, 1);
                if (got < 0) return cu_error("sort", name);
                if (got == 0 || c == '\n') { ended = 1; break; }
            } while (1);
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    cu_args args; cu_args_init(&args, argc, argv);
    char** lines = 0; size_t count = 0, capacity = 0;
    const char* value;
    while ((value = cu_args_next(&args)) != 0) {
        if (cu_is_help(value)) return cu_help("sort", "[-r] [-u] [FILE]...", "Sort lines of text; read standard input when no FILE is given.");
        if (cu_is_version(value)) return cu_version("sort");
        if (args.options && __builtin_strcmp(value, "-r") == 0) { reverse_order = 1; continue; }
        if (args.options && __builtin_strcmp(value, "-u") == 0) { unique_lines = 1; continue; }
        if (args.options && value[0] == '-') return cu_error("sort", "unknown option");
        int fd = __builtin_strcmp(value, "-") == 0 ? CU_STDIN : open(value, O_RDONLY);
        if (fd < 0) { cu_error("sort", value); return 1; }
        if (sort_fd(fd, value, &lines, &count, &capacity) != 0) {
            if (fd != CU_STDIN) close(fd);
            free_lines(lines, count);
            return 1;
        }
        if (fd != CU_STDIN) close(fd);
    }
    if (args.index == 1) {
        if (sort_fd(CU_STDIN, "standard input", &lines, &count, &capacity) != 0) {
            free_lines(lines, count);
            return 1;
        }
    }
    qsort(lines, count, sizeof(char*), compare_lines);
    for (size_t i = 0; i < count; ++i) {
        if (unique_lines && i != 0 && strcmp(lines[i], lines[i - 1]) == 0) { free(lines[i]); continue; }
        if (cu_write_all(CU_STDOUT, lines[i], strlen(lines[i])) < 0) {
            free(lines[i]);
            for (size_t j = i + 1; j < count; ++j)
                free(lines[j]);
            free(lines);
            return 1;
        }
        free(lines[i]);
    }
    free(lines);
    return 0;
}
