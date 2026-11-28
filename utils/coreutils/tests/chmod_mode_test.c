#define main chmod_program_main
#include "../src/chmod.c"
#undef main

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int failures;

static char         fixture_template[] = "/tmp/chmod-mode-XXXXXX";
static char*        fixture_root;
static unsigned int fixture_count;

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

static void expect_program(const char* what, const char* option, const char* mode_text,
                           mode_t start, int expected_status, mode_t expected) {
    char path[512];

    char* argv[5];

    int argc = 0;

    snprintf(path, sizeof(path), "%s/file-%u", fixture_root, ++fixture_count);

    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0600);

    if (fd < 0 || close(fd) != 0 || chmod(path, start) != 0) {
        printf("chmod mode test failed: %s: fixture setup\n", what);
        ++failures;

        return;
    }

    argv[argc++] = "chmod";

    if (option != 0)
        argv[argc++] = (char*) option;

    argv[argc++] = (char*) mode_text;
    argv[argc++] = path;
    argv[argc]   = 0;

    int status = chmod_program_main(argc, argv);

    struct stat info;

    if (status != expected_status || stat(path, &info) != 0 ||
        (info.st_mode & 07777) != expected) {
        printf("chmod mode test failed: %s: %s on %04o got status %d mode %04o expected "
               "status %d mode %04o\n",
               what, mode_text, (unsigned int) start, status,
               (unsigned int) (info.st_mode & 07777), expected_status,
               (unsigned int) expected);
        ++failures;
    }

    unlink(path);
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

    fixture_root = mkdtemp(fixture_template);
    if (fixture_root == 0) {
        printf("chmod mode test failed: fixture directory\n");

        return 1;
    }

    saved = umask(0);

    expect_program("leading -w", 0, "-w", 0666, 0, 0444);
    expect_program("leading +x", 0, "+x", 0644, 0, 0755);
    expect_program("leading =rx", 0, "=rx", 0644, 0, 0555);
    expect_program("invalid leading mode", 0, "-z", 0644, 1, 0644);
    expect_program("numeric mode", 0, "600", 0777, 0, 0600);
    expect_program("who mode", 0, "u-x", 0777, 0, 0677);
    expect_program("double dash", "--", "600", 0777, 0, 0600);

    umask(saved);

    if (rmdir(fixture_root) != 0) {
        printf("chmod mode test failed: fixture cleanup\n");
        ++failures;
    }

    if (failures != 0) {
        printf("%d chmod mode test(s) failed\n", failures);

        return 1;
    }

    printf("chmod mode tests ok\n");

    return 0;
}
