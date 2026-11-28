#include "common.h"
#include "ls_layout.h"
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    char*  name;
    size_t width;
    mode_t mode;
    int    has_mode;
} ls_entry;

typedef struct {
    int    detailed;
    int    color;
    int    columns;
    size_t width;
} ls_options;

static void mode_string(mode_t mode, char* result) {
    result[0] = S_ISDIR(mode) ? 'd' : S_ISLNK(mode) ? 'l' : S_ISREG(mode) ? '-' : '?';

    const int  bits[]  = {S_IRUSR, S_IWUSR, S_IXUSR, S_IRGRP, S_IWGRP,
                          S_IXGRP, S_IROTH, S_IWOTH, S_IXOTH};
    const char marks[] = "rwxrwxrwx";

    for (int i = 0; i < 9; ++i) {
        result[i + 1] = (mode & bits[i]) ? marks[i] : '-';
    }

    result[10] = '\0';
}

static char* join_path(const char* parent, const char* name) {
    size_t a = strlen(parent), b = strlen(name);

    char* result = malloc(a + b + 2);
    if (result == 0)
        return 0;

    memcpy(result, parent, a);

    result[a] = (a != 0 && parent[a - 1] == '/') ? '\0' : '/';

    if (result[a] == '/')
        ++a;

    memcpy(result + a, name, b + 1);

    return result;
}

static char* duplicate_name(const char* name) {
    size_t length = strlen(name) + 1;

    char* copy = malloc(length);
    if (copy != 0)
        memcpy(copy, name, length);

    return copy;
}

static const char* color_code(mode_t mode) {
    if (S_ISDIR(mode))
        return "\033[34m";

    if (S_ISLNK(mode))
        return "\033[36m";

    if (S_ISREG(mode) && (mode & (S_IXUSR | S_IXGRP | S_IXOTH)))
        return "\033[32m";

    return 0;
}

static int put_name(const char* name, mode_t mode, int has_mode, int color) {
    const char* code = color && has_mode ? color_code(mode) : 0;
    if (code == 0)
        return cu_puts(name);

    if (cu_puts(code) < 0 || cu_puts(name) < 0 || cu_puts("\033[0m") < 0)
        return -1;

    return 0;
}

static int put_spaces(size_t count) {
    static const char spaces[] = "                                ";

    while (count != 0) {
        size_t chunk = count < sizeof spaces - 1 ? count : sizeof spaces - 1;

        if (cu_write_all(CU_STDOUT, spaces, chunk) < 0)
            return -1;

        count -= chunk;
    }

    return 0;
}

static int print_symlink_target(const char* path) {
    size_t capacity = 256;

    char* target = malloc(capacity);

    if (target == 0)
        return cu_error("ls", "out of memory");

    for (;;) {
        long count = readlink(path, target, capacity);
        if (count < 0) {
            free(target);

            return cu_error("ls", strerror(errno));
        }

        if ((size_t) count < capacity) {
            int status = 0;

            if (cu_puts(" -> ") < 0 ||
                cu_write_all(CU_STDOUT, target, (size_t) count) < 0)
                status = 1;

            free(target);

            return status;
        }

        if (capacity >= PATH_MAX) {
            free(target);

            return cu_error("ls", strerror(ENAMETOOLONG));
        }

        size_t next = capacity * 2 < PATH_MAX ? capacity * 2 : PATH_MAX;

        char* grown = realloc(target, next);

        if (grown == 0) {
            free(target);

            return cu_error("ls", "out of memory");
        }

        target   = grown;
        capacity = next;
    }
}

static int print_detailed(const char* path, const char* name, int color) {
    struct stat info;

    if (lstat(path, &info) < 0)
        return cu_error("ls", strerror(errno));

    char mode[11];

    mode_string(info.st_mode, mode);

    char line[256];
    int  length =
        snprintf(line, sizeof line, "%s %lu %u %u %ld %ld ", mode, info.st_nlink,
                 info.st_uid, info.st_gid, (long) info.st_size, (long) info.st_mtime);

    if (length < 0 || (size_t) length >= sizeof line ||
        cu_write_all(CU_STDOUT, line, (size_t) length) < 0)
        return 1;

    if (put_name(name, info.st_mode, 1, color) < 0)
        return 1;

    if (S_ISLNK(info.st_mode) && print_symlink_target(path) != 0)
        return 1;

    return cu_puts("\n") < 0;
}

static int compare_entries(const void* left, const void* right) {
    return strcmp(((const ls_entry*) left)->name, ((const ls_entry*) right)->name);
}

static void free_entries(ls_entry* entries, size_t count) {
    if (entries == 0)
        return;

    for (size_t index = 0; index < count; ++index) {
        free(entries[index].name);
    }

    free(entries);
}

static int collect_entries(const char* path, DIR* directory, int color, ls_entry** result,
                           size_t* count) {
    ls_entry* entries = 0;

    size_t used     = 0;
    size_t capacity = 0;
    int    error    = 0;

    struct dirent* item;

    for (;;) {
        errno = 0;
        item  = readdir(directory);
        if (item == 0) {
            if (errno != 0)
                error = errno;

            break;
        }

        if (used == capacity) {
            size_t next_capacity = capacity == 0 ? 16 : capacity * 2;

            ls_entry* grown = realloc(entries, next_capacity * sizeof(ls_entry));
            if (grown == 0) {
                error = ENOMEM;

                break;
            }

            entries  = grown;
            capacity = next_capacity;
        }

        char* name = duplicate_name(item->d_name);
        if (name == 0) {
            error = ENOMEM;

            break;
        }

        entries[used].name     = name;
        entries[used].width    = strlen(name);
        entries[used].mode     = 0;
        entries[used].has_mode = 0;

        if (color) {
            char*       child = join_path(path, name);
            struct stat info;

            if (child != 0 && lstat(child, &info) == 0) {
                entries[used].mode     = info.st_mode;
                entries[used].has_mode = 1;
            }

            free(child);
        }

        ++used;
    }

    if (error != 0) {
        free_entries(entries, used);

        errno = error;

        return -1;
    }

    *result = entries;
    *count  = used;

    return 0;
}

static int print_lines(const ls_entry* entries, size_t count, int color) {
    for (size_t index = 0; index < count; ++index) {
        if (put_name(entries[index].name, entries[index].mode, entries[index].has_mode,
                     color) < 0 ||
            cu_puts("\n") < 0)
            return 1;
    }

    return 0;
}

static int print_columns(const ls_entry* entries, size_t count, size_t terminal_width,
                         int color) {
    size_t longest = 0;

    for (size_t index = 0; index < count; ++index) {
        if (entries[index].width > longest)
            longest = entries[index].width;
    }

    size_t column_count = cu_ls_column_count(longest, terminal_width);
    if (column_count > count)
        column_count = count;

    if (column_count <= 1)
        return print_lines(entries, count, color);

    size_t* widths        = malloc(count * sizeof(size_t));
    size_t* column_widths = malloc(column_count * sizeof(size_t));

    int status = 0;

    if (widths == 0 || column_widths == 0) {
        free(widths);
        free(column_widths);

        return cu_error("ls", "out of memory");
    }

    for (size_t index = 0; index < count; ++index) {
        widths[index] = entries[index].width;
    }

    size_t rows = cu_ls_row_count(count, column_count);

    for (size_t column = 0; column < column_count; ++column) {
        column_widths[column] = cu_ls_column_width(widths, count, rows, column);
    }

    for (size_t row = 0; row < rows && status == 0; ++row) {
        for (size_t column = 0; column < column_count; ++column) {
            size_t index = row + column * rows;
            if (index >= count)
                break;

            if (put_name(entries[index].name, entries[index].mode,
                         entries[index].has_mode, color) < 0) {
                status = 1;

                break;
            }

            if (row + (column + 1) * rows >= count)
                continue;

            size_t padding = column_widths[column] > entries[index].width
                                 ? column_widths[column] - entries[index].width
                                 : 0;

            if (put_spaces(padding + CU_LS_COLUMN_GAP) < 0) {
                status = 1;

                break;
            }
        }

        if (status == 0 && cu_puts("\n") < 0)
            status = 1;
    }

    free(widths);
    free(column_widths);

    return status;
}

static int list_directory(const char* path, int headers, const ls_options* options) {
    DIR* directory = opendir(path);
    if (directory == 0)
        return cu_error("ls", strerror(errno));

    if (headers && (cu_puts(path) < 0 || cu_puts(":\n") < 0)) {
        closedir(directory);

        return 1;
    }

    ls_entry* entries = 0;

    size_t count = 0;
    int    status;

    if (collect_entries(path, directory, options->color && !options->detailed, &entries,
                        &count) != 0) {
        status = cu_error("ls", strerror(errno));
    }
    else {
        if (count > 1)
            qsort(entries, count, sizeof(ls_entry), compare_entries);

        if (options->detailed) {
            status = 0;

            for (size_t index = 0; index < count; ++index) {
                char* child = join_path(path, entries[index].name);
                if (child == 0) {
                    status = cu_error("ls", "out of memory");

                    break;
                }

                int entry_status =
                    print_detailed(child, entries[index].name, options->color);

                free(child);

                if (entry_status != 0) {
                    status = entry_status;

                    break;
                }
            }
        }
        else if (options->columns) {
            status = print_columns(entries, count, options->width, options->color);
        }
        else {
            status = print_lines(entries, count, options->color);
        }
    }

    free_entries(entries, count);
    closedir(directory);

    return status;
}

static int is_directory_operand(const char* path, const ls_options* options) {
    struct stat info;

    if (lstat(path, &info) != 0)
        return 0;

    if (!options->detailed && S_ISLNK(info.st_mode)) {
        struct stat target;

        return stat(path, &target) == 0 && S_ISDIR(target.st_mode);
    }

    return S_ISDIR(info.st_mode);
}

static int list_one(const char* path, int headers, const ls_options* options) {
    struct stat single;

    int have_info = lstat(path, &single) == 0;

    if (!options->detailed && have_info && S_ISLNK(single.st_mode)) {
        struct stat target;

        if (stat(path, &target) == 0 && S_ISDIR(target.st_mode))
            return list_directory(path, headers, options);
    }

    if (have_info && !S_ISDIR(single.st_mode)) {
        const char* slash = strrchr(path, '/');
        const char* name  = slash ? slash + 1 : path;

        if (options->detailed)
            return print_detailed(path, name, options->color);

        return put_name(name, single.st_mode, 1, options->color) < 0 || cu_puts("\n") < 0;
    }

    return list_directory(path, headers, options);
}

static int list_operands(const char* const* operands, size_t count,
                         const ls_options* options) {
    int headers = count > 1;
    int printed = 0;
    int status  = 0;

    for (size_t index = 0; index < count; ++index) {
        if (is_directory_operand(operands[index], options))
            continue;

        if (list_one(operands[index], 0, options) == 0)
            printed = 1;
        else
            status = 1;
    }

    for (size_t index = 0; index < count; ++index) {
        if (!is_directory_operand(operands[index], options))
            continue;

        if (printed && cu_puts("\n") < 0)
            status = 1;

        if (list_one(operands[index], headers, options) == 0)
            printed = 1;
        else
            status = 1;
    }

    return status;
}

static void options_init(ls_options* options) {
    options->detailed = 0;
    options->color    = 0;
    options->columns  = 0;
    options->width    = 0;

    if (!isatty(CU_STDOUT))
        return;

    options->color = cu_ls_color_enabled(1, getenv("NO_COLOR") != 0);

    struct winsize window;

    if (ioctl(CU_STDOUT, TIOCGWINSZ, &window) == 0 && window.ws_col != 0) {
        options->columns = 1;
        options->width   = window.ws_col;
    }
}

int main(int argc, char** argv) {
    cu_args    args;
    ls_options options;

    cu_args_init(&args, argc, argv);
    options_init(&options);

    const char* path;

    if (argc == 1)
        return list_one(".", 0, &options);

    const char** operands = malloc((size_t) argc * sizeof *operands);
    if (operands == 0)
        return cu_error("ls", "out of memory");

    size_t operand_count = 0;

    while ((path = cu_args_next(&args)) != 0) {
        if (cu_is_help(path)) {
            free(operands);

            return cu_help("ls", "[OPTION]... [FILE]...",
                           "List directory contents.  -a and -l are supported.");
        }

        if (cu_is_version(path)) {
            free(operands);

            return cu_version("ls");
        }

        if (cu_arg_is(path, "-a", "--all"))
            continue;

        if (cu_arg_is(path, "-l", "--long")) {
            options.detailed = 1;

            continue;
        }

        if (path[0] == '-') {
            free(operands);

            return cu_error("ls", "unknown option");
        }

        operands[operand_count++] = path;
    }

    int status = operand_count == 0 ? list_one(".", 0, &options)
                                    : list_operands(operands, operand_count, &options);

    free(operands);

    return status;
}
