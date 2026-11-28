#include "units.h"
#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;

static void check(int condition, const char* expression, int line) {
    if (condition)
        return;

    fprintf(stderr, "FAIL units_test.c:%d: %s\n", line, expression);

    ++failures;
}

#define CHECK(condition) check((condition) != 0, #condition, __LINE__)

static char root[160];

static void make_root(void) {
    snprintf(root, sizeof root, "/tmp/astia-units-test-%d", (int) getpid());

    char command[200];

    snprintf(command, sizeof command, "rm -rf %s", root);
    (void) system(command);
    CHECK(mkdir(root, 0700) == 0);
}

static int write_file(const char* name, const char* text) {
    char path[240];

    snprintf(path, sizeof path, "%s/%s", root, name);

    FILE* file = fopen(path, "w");
    if (!file)
        return -1;

    fputs(text, file);
    fclose(file);

    return 0;
}

static int load(struct unit* units, int capacity, int* count) {
    return units_load(root, units, capacity, count);
}

static void remove_root(void) {
    char command[200];

    snprintf(command, sizeof command, "rm -rf %s", root);
    (void) system(command);
}

static void test_parse(void) {
    CHECK(write_file("README", "ignored\n") == 0);
    CHECK(write_file("aaa.yaml", "description: \"quoted description # not a comment\"\n"
                                 "exec: '/bin/echo'\n"
                                 "args:\n"
                                 "    - \"hello world\"\n"
                                 "    - plain\n"
                                 "restart: on-failure\n"
                                 "restart-limit: 2\n"
                                 "working-directory: /tmp\n"
                                 "environment:\n"
                                 "    PATH: /bin:/usr/bin\n"
                                 "    EMPTY: \"\"\n") == 0);
    CHECK(write_file("zzz.yaml", "exec: /bin/true\n") == 0);

    struct unit units[4];

    int count = 0;

    CHECK(load(units, 4, &count) == 0);
    CHECK(count == 2);

    if (count != 2) {
        remove_root();

        return;
    }

    CHECK(!strcmp(units[0].name, "aaa"));
    CHECK(!strcmp(units[0].exec, "/bin/echo"));
    CHECK(units[0].argc == 2);
    CHECK(!strcmp(units[0].argv[0], "/bin/echo"));
    CHECK(!strcmp(units[0].argv[1], "hello world"));
    CHECK(!strcmp(units[0].argv[2], "plain"));
    CHECK(units[0].argv[3] == NULL);
    CHECK(units[0].restart == 1);
    CHECK(units[0].failure_only == 1);
    CHECK(units[0].max_restarts == 2);
    CHECK(!strcmp(units[0].working_directory, "/tmp"));
    CHECK(units[0].envc == 2);
    CHECK(!strcmp(units[0].envp[0], "PATH=/bin:/usr/bin"));
    CHECK(!strcmp(units[0].envp[1], "EMPTY="));
    CHECK(units[0].envp[2] == NULL);

    CHECK(!strcmp(units[1].name, "zzz"));
    CHECK(units[1].argc == 0);
    CHECK(units[1].restart == 0);
    CHECK(units[1].max_restarts == 0);
}

static void test_default_restarts(void) {
    CHECK(write_file("only.yaml", "exec: /bin/true\nrestart: always\n") == 0);

    struct unit units[2];

    int count = 0;

    CHECK(load(units, 2, &count) == 0);
    CHECK(count == 1);
    CHECK(count == 1 && units[0].restart == 1);
    CHECK(count == 1 && units[0].max_restarts == UNIT_DEFAULT_RESTARTS);

    char path[240];

    snprintf(path, sizeof path, "%s/only.yaml", root);
    unlink(path);
}

static void test_rejected(const char* text) {
    CHECK(write_file("bad.yaml", text) == 0);

    struct unit units[2];

    int count = 0;

    CHECK(load(units, 2, &count) < 0);

    char path[240];

    snprintf(path, sizeof path, "%s/bad.yaml", root);
    unlink(path);
}

static void test_missing_directory(void) {
    struct unit units[1];

    int count = 1;

    CHECK(units_load("/tmp/astia-units-test-missing", units, 1, &count) == 0);
    CHECK(count == 0);
}

int main(void) {
    make_root();
    test_parse();

    char command[200];

    snprintf(command, sizeof command, "rm -rf %s/*", root);
    (void) system(command);

    test_default_restarts();

    snprintf(command, sizeof command, "rm -rf %s/*", root);
    (void) system(command);

    test_rejected("exec: /bin/true\nmemory-limit: 1G\n");
    test_rejected("restart: always\n");
    test_rejected("exec: bin/true\n");
    test_rejected("exec: /bin/true\nexec: /bin/false\n");
    test_rejected("exec: /bin/true\nrestart: sometimes\n");
    test_rejected("exec: /bin/true\nrestart-limit: 99\n");
    test_rejected("exec: /bin/true\nargs:\n\t- value\n");
    test_rejected("exec: /bin/true\n  stray\n");

    test_missing_directory();
    remove_root();

    if (failures) {
        fprintf(stderr, "units_test: %d failure(s)\n", failures);

        return 1;
    }

    printf("units_test: passed\n");

    return 0;
}
