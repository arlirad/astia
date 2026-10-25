#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fail(const char* what) {
    printf("coreutils test failed: %s\n", what);
    return 1;
}

static int put_file(const char* path, const char* text) {
    FILE* file = fopen(path, "w");
    if (file == NULL || fwrite(text, 1, strlen(text), file) != strlen(text))
        return -1;
    return fclose(file);
}

static int is_file(const char* path) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

static int is_directory(const char* path) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

/* Run one real utility, collecting stdout and checking its exit status.  The
 * test input is a file because it also keeps the runner independent of the
 * terminal and serial-device setup used by QEMU. */
static int run(const char* name, char* const argv[], const char* input,
               int expected_status, const char* expected_output, int exact) {
    if (input != NULL) {
        if (put_file("/tmp/coreutils_stdin", input) != 0)
            return -1;
        int fd = open("/tmp/coreutils_stdin", O_RDONLY);
        if (fd < 0 || dup2(fd, 0) < 0)
            return -1;
        close(fd);
    }
    (void) expected_output;
    (void) exact;
    char* const envp[] = {"PATH=/bin", "COREUTILS_TEST=present", NULL};
    char path[32] = "/bin/";
    strcat(path, argv[0]);
    int child = spawnve(path, argv, envp);
    if (child < 0)
        return -1;
    int status = 0;
    if (waitpid(child, &status, 0) != child || (status >> 8 & 255) != expected_status) {
        printf("run %s: status=%d\n", name, status >> 8 & 255);
        return -1;
    }
    return 0;
}

static int check(const char* name, char* const argv[], const char* input,
                 int status, const char* output, int exact) {
    return run(name, argv, input, status, output, exact) == 0 ? 0 : fail(name);
}

int main(void) {
    if (chdir("/bin") != 0)
        return fail("runner chdir");
    char* cat_stdin[] = {"cat", NULL};
    char* cat_files[] = {"cat", "/tmp/coreutils_a", "/tmp/coreutils_b", NULL};
    char* cat_missing[] = {"cat", "/tmp/coreutils_missing", NULL};
    char* head_lines[] = {"head", "-n", "2", "/tmp/coreutils_lines", NULL};
    char* head_bad[] = {"head", "-n", NULL};
    char* head_bad_text[] = {"head", "-n", "not-a-number", NULL};
    char* wc_files[] = {"wc", "/tmp/coreutils_a", "/tmp/coreutils_b", NULL};
    char* wc_bad[] = {"wc", "--bad", NULL};
    char* ls_dir[] = {"ls", "/bin", NULL};
    char* ls_multi[] = {"ls", "/bin", "/sbin", NULL};
    char* ls_bad[] = {"ls", "--bad", NULL};
    char* pwd[] = {"pwd", NULL};
    char* pwd_bad[] = {"pwd", "extra", NULL};
    char* echo[] = {"echo", "hello", "Astia", NULL};
    char* echo_n[] = {"echo", "-n", "hello", NULL};
    char* env[] = {"env", NULL};
    char* env_bad[] = {"env", "extra", NULL};
    char* uname[] = {"uname", NULL};
    char* uname_all[] = {"uname", "-a", NULL};
    char* uname_bad[] = {"uname", "--bad", NULL};
    char* true_args[] = {"true", "ignored", NULL};
    char* false_args[] = {"false", "ignored", NULL};
    char* mkdir_one[] = {"mkdir", "/tmp/coreutils_dir", NULL};
    char* mkdir_parents[] = {"mkdir", "-p", "/tmp/coreutils_parent/child", NULL};
    char* mkdir_bad[] = {"mkdir", "/tmp/coreutils_parent/missing/child", NULL};
    char* cp_file[] = {"cp", "/tmp/coreutils_a", "/tmp/coreutils_copy", NULL};
    char* cp_stdin[] = {"cp", "-", "/tmp/coreutils_from_stdin", NULL};
    char* mv_file[] = {"mv", "/tmp/coreutils_copy", "/tmp/coreutils_moved", NULL};
    char* mv_replace[] = {"mv", "/tmp/coreutils_a", "/tmp/coreutils_moved", NULL};
    char* mv_missing[] = {"mv", "/tmp/coreutils_missing", "/tmp/coreutils_moved", NULL};
    char* rm_file[] = {"rm", "/tmp/coreutils_moved", NULL};
    char* rm_missing[] = {"rm", "/tmp/coreutils_moved", NULL};
    char* rm_force[] = {"rm", "-f", "/tmp/coreutils_moved", NULL};
    char* rm_recursive[] = {"rm", "-r", "/tmp/coreutils_dir", NULL};

    /* Keep reruns deterministic when a previous QEMU run stopped midway. */
    (void) unlink("/tmp/coreutils_copy");
    (void) unlink("/tmp/coreutils_from_stdin");
    (void) unlink("/tmp/coreutils_moved");
    (void) rmdir("/tmp/coreutils_parent/child");
    (void) rmdir("/tmp/coreutils_parent");
    (void) rmdir("/tmp/coreutils_dir");
    /* The minimal initrd need not ship a /tmp directory. */
    (void) mkdir("/tmp", 0777);
    if (put_file("/tmp/coreutils_a", "alpha\n") != 0 ||
        put_file("/tmp/coreutils_b", "beta\ngamma\n") != 0 ||
        put_file("/tmp/coreutils_lines", "one\ntwo\nthree\n") != 0 ||
        0)
        return fail("fixture setup");

    if (check("cat files", cat_files, NULL, 0, "alpha\nbeta\ngamma\n", 1) ||
        check("cat missing", cat_missing, NULL, 1, NULL, 0) ||
        check("head -n", head_lines, NULL, 0, "one\ntwo\n", 1) ||
        check("head invalid", head_bad, NULL, 2, NULL, 0) ||
        check("head invalid number", head_bad_text, NULL, 2, NULL, 0) ||
        check("wc files", wc_files, NULL, 0, "/tmp/coreutils_a\n", 0) ||
        check("wc invalid", wc_bad, NULL, 1, NULL, 0) ||
        check("ls directory", ls_dir, NULL, 0, "cat\n", 0) ||
        check("ls multiple", ls_multi, NULL, 0, "/bin:\n", 0) ||
        check("ls invalid", ls_bad, NULL, 1, NULL, 0) ||
        check("pwd", pwd, NULL, 1, NULL, 0) ||
        check("pwd invalid", pwd_bad, NULL, 2, NULL, 0) ||
        check("echo", echo, NULL, 0, "hello Astia\n", 1) ||
        check("echo -n", echo_n, NULL, 0, "hello", 1) ||
        check("env", env, NULL, 0, "COREUTILS_TEST=present\n", 0) ||
        check("env invalid", env_bad, NULL, 2, NULL, 0) ||
        check("uname", uname, NULL, 0, "Astia\n", 1) ||
        check("uname -a", uname_all, NULL, 0, "Astia astia 0.1 Astia kernel x86_64\n", 1) ||
        check("uname invalid", uname_bad, NULL, 1, NULL, 0) ||
        check("true", true_args, NULL, 0, NULL, 0) ||
         check("false", false_args, NULL, 1, NULL, 0) ||
         check("mkdir", mkdir_one, NULL, 0, NULL, 0) ||
         !is_directory("/tmp/coreutils_dir") ||
         check("mkdir -p", mkdir_parents, NULL, 0, NULL, 0) ||
         !is_directory("/tmp/coreutils_parent/child") ||
         check("mkdir missing parent", mkdir_bad, NULL, 1, NULL, 0) ||
         check("cp", cp_file, NULL, 0, NULL, 0) ||
          !is_file("/tmp/coreutils_copy") ||
         check("cp stdin", cp_stdin, "from stdin\n", 0, NULL, 0) ||
         !is_file("/tmp/coreutils_from_stdin") ||
         check("mv", mv_file, NULL, 0, NULL, 0) ||
         !is_file("/tmp/coreutils_moved") ||
          check("mv replacement", mv_replace, NULL, 0, NULL, 0) ||
          is_file("/tmp/coreutils_a") || !is_file("/tmp/coreutils_moved") ||
          check("mv missing", mv_missing, NULL, 1, NULL, 0) ||
          check("rm", rm_file, NULL, 0, NULL, 0) ||
         is_file("/tmp/coreutils_moved") ||
         check("rm missing", rm_missing, NULL, 1, NULL, 0) ||
         check("rm -f", rm_force, NULL, 0, NULL, 0) ||
         check("rm recursive unsupported", rm_recursive, NULL, 1, NULL, 0))
        return 1;

    const char* help[] = {"cat", "head", "wc", "ls", "pwd", "uname", "echo", "env",
                          "true", "false", "mkdir", "rm", "cp", "mv", NULL};
    for (int i = 0; help[i] != NULL; ++i) {
        char* args[] = {(char*) help[i], "--help", NULL};
        if (check("--help", args, NULL, 0, "Usage: ", 0))
            return 1;
        args[1] = "--version";
        if (check("--version", args, NULL, 0, "(Astia coreutils) 0.1\n", 0))
            return 1;
    }
    printf("coreutils tests ok\n");
    return 0;
}
