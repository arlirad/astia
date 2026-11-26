#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int failures;

#define CHECK(condition)                                                \
    do {                                                                \
        if (!(condition)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

static void test_cluster(void) {
    char* argv[] = {(char*) "prog", (char*) "-abc", (char*) "file", NULL};

    optind = 1;
    opterr = 0;

    CHECK(getopt(3, argv, "abc") == 'a');
    CHECK(optind == 1 && optarg == NULL);
    CHECK(getopt(3, argv, "abc") == 'b');
    CHECK(optind == 1);
    CHECK(getopt(3, argv, "abc") == 'c');
    CHECK(optind == 2);
    CHECK(getopt(3, argv, "abc") == -1);
    CHECK(optind == 2);
}

static void test_arguments(void) {
    char* argv[] = {(char*) "prog",     (char*) "-abVALUE", (char*) "-o",
                    (char*) "separate", (char*) "file",     NULL};

    optind = 1;

    CHECK(getopt(5, argv, "ab:o:") == 'a');
    CHECK(getopt(5, argv, "ab:o:") == 'b' && strcmp(optarg, "VALUE") == 0);
    CHECK(getopt(5, argv, "ab:o:") == 'o' && strcmp(optarg, "separate") == 0);
    CHECK(optind == 4);
    CHECK(getopt(5, argv, "ab:o:") == -1 && optind == 4);
}

static void test_termination(void) {
    char* argv[]      = {(char*) "prog", (char*) "-a", (char*) "--", (char*) "-b", NULL};
    char* dash_argv[] = {(char*) "prog", (char*) "-", (char*) "-a", NULL};

    optind = 1;

    CHECK(getopt(4, argv, "ab") == 'a');
    CHECK(getopt(4, argv, "ab") == -1 && optind == 3);
    CHECK(getopt(4, argv, "ab") == -1 && optind == 3);

    optind = 1;

    CHECK(getopt(3, dash_argv, "a") == -1 && optind == 1);
}

static void test_errors(void) {
    char* unknown[]        = {(char*) "prog", (char*) "-x", NULL};
    char* missing[]        = {(char*) "prog", (char*) "-o", NULL};
    char* leading_colon[]  = {(char*) "prog", (char*) "-o", NULL};
    char* attached_empty[] = {(char*) "prog", (char*) "-o", NULL};

    optind = 1;
    opterr = 0;

    CHECK(getopt(2, unknown, "a") == '?');
    CHECK(optopt == 'x' && optarg == NULL && optind == 2);

    optind = 1;

    CHECK(getopt(2, missing, "o:") == '?');
    CHECK(optopt == 'o' && optarg == NULL && optind == 3);

    optind = 1;

    CHECK(getopt(2, leading_colon, ":o:") == ':');
    CHECK(optopt == 'o' && optarg == NULL && optind == 3);

    optind = 1;

    CHECK(getopt(2, attached_empty, "o:") == '?');
    CHECK(optopt == 'o' && optarg == NULL);
}

static void test_reset_and_boundaries(void) {
    char* first[]    = {(char*) "prog", (char*) "-a", NULL};
    char* second[]   = {(char*) "prog", (char*) "-b", NULL};
    char* argument[] = {(char*) "prog", (char*) "-o", (char*) "--", NULL};
    char* empty[]    = {(char*) "prog", NULL};

    optind = 1;

    CHECK(getopt(2, first, "a") == 'a');

    optind = 1;

    CHECK(getopt(2, second, "b") == 'b');

    optind = 1;

    CHECK(getopt(3, argument, "o:") == 'o');
    CHECK(strcmp(optarg, "--") == 0 && optind == 3);

    optind = 1;

    CHECK(getopt(1, empty, "a") == -1 && optind == 1);
}

int main(void) {
    test_cluster();
    test_arguments();
    test_termination();
    test_errors();
    test_reset_and_boundaries();

    return failures != 0;
}
