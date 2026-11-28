#include "../src/ls_mode.h"
#include "ls_mode_cases.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void expect_case(const ls_mode_case* test) {
    char actual[11];

    cu_ls_mode_string(test->mode, actual);

    if (strcmp(actual, test->expected) != 0) {
        printf("ls mode test failed: %s: got %s expected %s\n", test->label, actual,
               test->expected);
        ++failures;
    }
}

int main(void) {
    for (size_t index = 0; index < sizeof ls_mode_cases / sizeof ls_mode_cases[0];
         ++index) {
        expect_case(&ls_mode_cases[index]);
    }

    if (failures != 0)
        return 1;

    printf("ls mode tests ok\n");

    return 0;
}
