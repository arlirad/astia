#define main chmod_program_main
#include "../src/chmod.c"
#undef main

#include <stdio.h>

static int failures;

static void expect(const char* what, const char* mode_text, mode_t start, int directory,
                   mode_t expected) {
    mode_t actual = 0;

    int status = parse_mode(mode_text, start, directory, &actual);

    if (status == 0 && actual == expected)
        return;

    printf("chmod mode test failed: %s: %s on %04o got %04o (status %d) expected %04o\n",
           what, mode_text, (unsigned int) start, (unsigned int) actual, status,
           (unsigned int) expected);
    ++failures;
}

int main(void) {
    expect("u=rw clears setuid", "u=rw", 04755, 0, 0655);
    expect("u= clears setuid", "u=", 04755, 0, 0055);
    expect("g=rw clears setgid", "g=rw", 06755, 0, 04765);
    expect("g= clears setgid", "g=", 06755, 0, 04705);
    expect("o= clears sticky", "o=", 01755, 0, 00750);
    expect("u=rw keeps setgid", "u=rw", 06755, 0, 02655);
    expect("go= clears group and other", "go=", 04755, 0, 04700);
    expect("a= clears all special bits", "a=", 04755, 0, 0000);
    expect("a=rx", "a=rx", 04755, 0, 0555);
    expect("a=s", "a=s", 04755, 0, 06000);
    expect("u=u copies user bits", "u=u", 04755, 0, 0755);
    expect("g=u copies user bits", "g=u", 04755, 0, 04775);
    expect("u=s clears then sets setuid", "u=s", 04755, 0, 04055);
    expect("u=t ignores sticky", "u=t", 04755, 0, 00055);
    expect("u=rw,g=r", "u=rw,g=r", 04755, 0, 0645);
    expect("u=rw,g=rw on setgid file", "u=rw,g=rw", 02755, 0, 0665);
    expect("a+X without execute", "a+X", 0644, 0, 0644);
    expect("a+X with execute", "a+X", 0755, 0, 0755);
    expect("a+X on directory", "a+X", 0644, 1, 0755);
    expect("a+X after clearing execute", "u-x,a+X", 0700, 0, 0600);
    expect("a=X before clearing", "a=X", 04755, 0, 0111);
    expect("u=rwX", "u=rwX", 04755, 0, 0755);

    mode_t saved = umask(022);

    expect("=rw with umask", "=rw", 04755, 0, 0644);
    expect("=rwx with umask", "=rwx", 04755, 0, 0755);
    expect("=s with umask", "=s", 04755, 0, 06000);

    umask(saved);

    expect("numeric mode", "4755", 0, 0, 04755);
    expect("numeric mode ignores current", "600", 04755, 0, 0600);

    if (failures != 0) {
        printf("%d chmod mode test(s) failed\n", failures);

        return 1;
    }

    printf("chmod mode tests ok\n");

    return 0;
}
