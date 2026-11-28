#define main find_program_main
#include "../src/find.c"
#undef main

#define main chmod_program_main
#include "../src/chmod.c"
#undef main

#define main cp_program_main
#include "../src/cp.c"
#undef main

#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>

enum {
    deep_depth      = 1600,
    walk_stack_size = 128 * 1024,
};

static char source_root[PATH_MAX];
static char copy_root[PATH_MAX];

static int is_mode(const char* path, mode_t expected) {
    struct stat info;

    return stat(path, &info) == 0 && (info.st_mode & 07777) == expected;
}

struct walk_call {
    int (*function)(void);
    int result;
};

static void* walk_thread(void* argument) {
    struct walk_call* call = (struct walk_call*) argument;

    call->result = call->function();

    return 0;
}

static int run_with_small_stack(int (*function)(void)) {
    pthread_attr_t attributes;
    pthread_t      thread;

    struct walk_call call = {function, 1};

    if (pthread_attr_init(&attributes) != 0)
        return -1;

    if (pthread_attr_setstacksize(&attributes, walk_stack_size) != 0) {
        (void) pthread_attr_destroy(&attributes);

        return -1;
    }

    int created = pthread_create(&thread, &attributes, walk_thread, &call);

    (void) pthread_attr_destroy(&attributes);

    if (created != 0)
        return -1;

    if (pthread_join(thread, 0) != 0)
        return -1;

    return call.result;
}

static int build_deep_tree(const char* root) {
    size_t length = strlen(root);

    if (length + 3 >= sizeof source_root)
        return -1;

    memcpy(source_root, root, length + 1);

    char path[PATH_MAX];

    memcpy(path, root, length + 1);

    if (mkdir(path, 0755) != 0)
        return -1;

    for (int depth = 0; depth < deep_depth; ++depth) {
        if (length + 2 >= sizeof path)
            return -1;

        path[length++] = '/';
        path[length++] = 'd';
        path[length]   = '\0';

        if (mkdir(path, 0755) != 0)
            return -1;
    }

    if (length + 2 >= sizeof path)
        return -1;

    path[length++] = '/';
    path[length++] = 'f';
    path[length]   = '\0';

    FILE* file = fopen(path, "w");
    if (file == 0)
        return -1;

    int status = fputs("deep\n", file) < 0 ? -1 : 0;

    if (fclose(file) != 0)
        status = -1;

    return status;
}

static int find_case(void) {
    int saved_stdout = dup(1);
    int null_output  = open("/dev/null", O_WRONLY);

    if (saved_stdout < 0 || null_output < 0 || dup2(null_output, 1) < 0)
        return -1;

    close(null_output);

    int status = find_walk(source_root, follow_never);

    if (dup2(saved_stdout, 1) < 0)
        status = -1;

    close(saved_stdout);

    return status;
}

static int chmod_case(void) {
    return change_tree(source_root, "700", 1);
}

static int cp_case(void) {
    return copy_tree(source_root, copy_root);
}

static int deepest_path(char* path, size_t capacity, const char* root) {
    int written = snprintf(path, capacity, "%s", root);

    if (written < 0 || (size_t) written >= capacity)
        return -1;

    size_t length = (size_t) written;

    for (int depth = 0; depth < deep_depth; ++depth) {
        if (length + 2 >= capacity)
            return -1;

        path[length++] = '/';
        path[length++] = 'd';
        path[length]   = '\0';
    }

    return 0;
}

static int verify_tree(void) {
    char path[PATH_MAX];

    if (deepest_path(path, sizeof path, source_root) != 0 || !is_mode(path, 0700))
        return -1;

    if (deepest_path(path, sizeof path, copy_root) != 0 || !is_mode(path, 0700))
        return -1;

    size_t length = strlen(path);

    if (length + 2 >= sizeof path)
        return -1;

    path[length++] = '/';
    path[length++] = 'f';
    path[length]   = '\0';

    FILE* file = fopen(path, "r");
    if (file == 0)
        return -1;

    char text[16] = {0};

    int status = fread(text, 1, sizeof text - 1, file) == 5 && strcmp(text, "deep\n") == 0
                     ? 0
                     : -1;

    if (fclose(file) != 0)
        status = -1;

    return status;
}

int main(void) {
    char command[2 * PATH_MAX + 32];

    char root[PATH_MAX];

    int written =
        snprintf(root, sizeof root, "/tmp/astia-tree-walk-%ld", (long) getpid());

    if (written < 0 || (size_t) written >= sizeof root)
        return 1;

    if (snprintf(copy_root, sizeof copy_root, "%s-copy", root) >= (int) sizeof copy_root)
        return 1;

    (void) snprintf(command, sizeof command, "rm -rf -- '%s' '%s'", root, copy_root);
    (void) system(command);

    if (build_deep_tree(root) != 0)
        return 1;

    if (run_with_small_stack(find_case) != 0 || run_with_small_stack(chmod_case) != 0 ||
        run_with_small_stack(cp_case) != 0 || verify_tree() != 0) {
        (void) snprintf(command, sizeof command, "rm -rf -- '%s' '%s'", root, copy_root);
        (void) system(command);

        return 1;
    }

    (void) snprintf(command, sizeof command, "rm -rf -- '%s' '%s'", root, copy_root);

    return system(command) == 0 ? 0 : 1;
}
