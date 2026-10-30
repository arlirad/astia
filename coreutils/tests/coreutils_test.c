#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>


static int fail(const char* what) {
    printf("coreutils test failed: %s\n", what);
    return 1;
}

static int put_file(const char* path, const char* text) {
    FILE* file = fopen(path, "w");
    if (file == NULL)
        return -1;
    if (fwrite(text, 1, strlen(text), file) != strlen(text)) {
        fclose(file);
        return -1;
    }
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

static int file_equals(const char* path, const char* expected) {
    FILE* file = fopen(path, "r");
    char buffer[512];
    size_t length = strlen(expected);
    if (file == NULL)
        return 0;
    if (length >= sizeof buffer || fread(buffer, 1, length, file) != length) {
        fclose(file);
        return 0;
    }
    buffer[length] = '\0';
    int equal = strcmp(buffer, expected) == 0;
    fclose(file);
    return equal;
}

static int credential_checks(void) {
    const char* file = "/tmp/coreutils_owner";
    const char* link = "/tmp/coreutils_owner_link";
    struct stat st;
    gid_t groups[16];
    int count = getgroups(0, NULL);
    if (count < 0 || count > 16 || setgroups(-1, groups) != -1 || errno != EINVAL)
        return fail("groups validation");
    if (count != 0 && getgroups(count, groups) != count)
        return fail("getgroups");
    (void) unlink(file);
    (void) unlink(link);
    int fd = open(file, O_CREAT | O_RDWR, 0640);
    if (fd < 0 || fstat(fd, &st) != 0 || st.st_uid != getuid() || st.st_gid != getgid())
        return fail("ownership inheritance");
    if (chown(file, getuid(), getgid()) != 0 ||
        symlink(file, link) != 0 || lchown(link, getuid(), getgid()) != 0 ||
        lstat(link, &st) != 0 || !S_ISLNK(st.st_mode))
        return fail("ownership APIs");
    close(fd);
    unlink(link);
    unlink(file);
    return 0;
}

static int account_checks(void) {
    struct passwd* root = getpwnam("root");
    struct passwd* by_uid = getpwuid(0);
    struct group* users = getgrnam("users");
    char login[32];
    if (root == NULL || by_uid == NULL || users == NULL ||
        strcmp(root->pw_name, "root") != 0 || by_uid->pw_uid != 0 ||
        users->gr_gid != 1000 || getlogin_r(login, sizeof login) != 0 ||
        strcmp(login, "root") != 0)
        return fail("account lookup");
    return 0;
}

/* Run one real utility, collecting stdout and checking its exit status and
 * output.  The test input is a file because it also keeps the runner
 * independent of the terminal and serial-device setup used by QEMU. */
static int run(const char* name, char* const argv[], const char* input,
               int expected_status, const char* expected_output, int exact) {
    if (input != NULL) {
        if (put_file("/tmp/coreutils_stdin", input) != 0)
            return -1;
        int fd = open("/tmp/coreutils_stdin", O_RDONLY);
        if (fd < 0)
            return -1;
        if (dup2(fd, 0) < 0) {
            close(fd);
            return -1;
        }
        close(fd);
    }
    char path[64];
    if (strlen(argv[0]) + 6 >= sizeof path)
        return -1;
    snprintf(path, sizeof path, "/bin/%s", argv[0]);

    int saved_stdout = -1;
    int output_fd = -1;
    if (expected_output != NULL) {
        saved_stdout = dup(1);
        output_fd = open("/tmp/coreutils_stdout", O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (saved_stdout < 0 || output_fd < 0 || dup2(output_fd, 1) < 0) {
            if (output_fd >= 0) close(output_fd);
            if (saved_stdout >= 0) close(saved_stdout);
            return -1;
        }
        close(output_fd);
    }
    char* const envp[] = {"PATH=/bin", "COREUTILS_TEST=present", NULL};
    int child = spawnve(path, argv, envp);
    if (child < 0) {
        if (saved_stdout >= 0) (void) dup2(saved_stdout, 1);
        if (saved_stdout >= 0) close(saved_stdout);
        return -1;
    }
    int status = 0;
    int ok = waitpid(child, &status, 0) == child &&
             ((status >> 8) & 255) == expected_status;
    if (saved_stdout >= 0) {
        if (dup2(saved_stdout, 1) < 0) ok = 0;
        close(saved_stdout);
    }
    if (!ok) {
        printf("run %s: status=%d\n", name, status >> 8 & 255);
        return -1;
    }
    if (expected_output != NULL && !file_equals("/tmp/coreutils_stdout", expected_output)) {
        if (exact == 0) {
            FILE* output = fopen("/tmp/coreutils_stdout", "r");
            char actual[512] = {0};
            if (output == NULL || fread(actual, 1, sizeof actual - 1, output) == 0 ||
                strstr(actual, expected_output) == NULL) {
                if (output != NULL) fclose(output);
                return -1;
            }
            fclose(output);
        } else {
            return -1;
        }
    }
    return 0;
}

static int check(const char* name, char* const argv[], const char* input,
                 int status, const char* output, int exact) {
    return run(name, argv, input, status, output, exact) == 0 ? 0 : fail(name);
}

int main(void) {
    if (credential_checks() != 0)
        return 1;
    if (account_checks() != 0)
        return 1;
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
    char* ls_long[] = {"ls", "-l", "/tmp/coreutils_link", NULL};
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
    char* cp_directory[] = {"cp", "-r", "/tmp/coreutils_tree", "/tmp/coreutils_tree_copy", NULL};
    char* cp_directory_bad[] = {"cp", "/tmp/coreutils_tree", "/tmp/coreutils_tree_bad", NULL};
    char* cp_stdin[] = {"cp", "-", "/tmp/coreutils_from_stdin", NULL};
    char* mv_file[] = {"mv", "/tmp/coreutils_copy", "/tmp/coreutils_moved", NULL};
    char* mv_replace[] = {"mv", "/tmp/coreutils_a", "/tmp/coreutils_moved", NULL};
    char* mv_missing[] = {"mv", "/tmp/coreutils_missing", "/tmp/coreutils_moved", NULL};
    char* rm_file[] = {"rm", "/tmp/coreutils_moved", NULL};
    char* rm_missing[] = {"rm", "/tmp/coreutils_moved", NULL};
    char* rm_force[] = {"rm", "-f", "/tmp/coreutils_moved", NULL};
    char* rm_recursive[] = {"rm", "-r", "/tmp/coreutils_dir", NULL};
    char* rm_tree[] = {"rm", "-r", "/tmp/coreutils_tree_copy", NULL};
    char* rm_tree_bad[] = {"rm", "-r", "/tmp/coreutils_tree_copy", NULL};
    char* find_tree[] = {"find", "/tmp/coreutils_tree", NULL};
    char* ls_tree[] = {"ls", "/tmp/coreutils_tree", NULL};
    char* grep_match[] = {"grep", "beta", NULL};
    char* grep_numbered[] = {"grep", "-n", "beta", NULL};
    char* grep_insensitive[] = {"grep", "-i", "BETA", NULL};
    char* grep_empty_insensitive[] = {"grep", "-i", "", NULL};
    char* grep_inverted[] = {"grep", "-v", "beta", NULL};
    char* grep_none[] = {"grep", "missing", NULL};
    char* grep_missing[] = {"grep", "pattern", "/tmp/coreutils_missing", NULL};
    char* sort_stdin[] = {"sort", NULL};
    char* cut_fields[] = {"cut", "-d", ":", "-f", "2", NULL};
    char* tee_file[] = {"tee", "/tmp/coreutils_tee", NULL};
    char* ln_link[] = {"ln", "coreutils_a", "/tmp/coreutils_link", NULL};
    char* readlink_link[] = {"readlink", "/tmp/coreutils_link", NULL};
    char* whoami[] = {"whoami", NULL};
    char* id[] = {"id", NULL};
    char* login[] = {"login", NULL};

    /* Keep reruns deterministic when a previous QEMU run stopped midway. */
    (void) unlink("/tmp/coreutils_copy");
    (void) unlink("/tmp/coreutils_from_stdin");
    (void) unlink("/tmp/coreutils_moved");
    (void) unlink("/tmp/coreutils_tee");
    (void) unlink("/tmp/coreutils_touch");
    (void) unlink("/tmp/coreutils_link");
    (void) unlink("/tmp/coreutils_dangling");
    (void) rmdir("/tmp/coreutils_parent/child");
    (void) rmdir("/tmp/coreutils_parent");
    (void) rmdir("/tmp/coreutils_dir");
    (void) unlink("/tmp/coreutils_tree_copy/nested/file");
    (void) rmdir("/tmp/coreutils_tree_copy/nested");
    (void) rmdir("/tmp/coreutils_tree_copy");
    (void) unlink("/tmp/coreutils_tree/nested/file");
    (void) rmdir("/tmp/coreutils_tree/nested");
    (void) rmdir("/tmp/coreutils_tree");
    (void) rmdir("/tmp/coreutils_tree_bad");
    /* The minimal initrd need not ship a /tmp directory. */
    (void) mkdir("/tmp", 0777);
    if (put_file("/tmp/coreutils_a", "alpha\n") != 0 ||
         put_file("/tmp/coreutils_b", "beta\ngamma\n") != 0 ||
         put_file("/tmp/coreutils_lines", "one\ntwo\nthree\n") != 0 ||
        0)
        return fail("fixture setup");

    struct stat dangling_stat;

    if (mkdir("/tmp/coreutils_tree", 0777) != 0 ||
        mkdir("/tmp/coreutils_tree/nested", 0777) != 0 ||
        put_file("/tmp/coreutils_tree/nested/file", "tree\n") != 0)
        return fail("recursive fixture setup");

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
         check("ln relative", ln_link, NULL, 0, NULL, 0) ||
         !is_file("/tmp/coreutils_link") ||
         check("readlink", readlink_link, NULL, 0, "coreutils_a\n", 1) ||
         check("ls -l symlink", ls_long, NULL, 0, " -> coreutils_a\n", 0) ||
         symlink("missing", "/tmp/coreutils_dangling") != 0 ||
         lstat("/tmp/coreutils_dangling", &dangling_stat) != 0 ||
         !S_ISLNK(dangling_stat.st_mode) ||
         stat("/tmp/coreutils_dangling", &dangling_stat) == 0 ||
         check("pwd", pwd, NULL, 0, "/bin\n", 1) ||
         check("whoami", whoami, NULL, 0, "root\n", 1) ||
         check("id", id, NULL, 0, "uid=0(root) gid=0(root)", 0) ||
         check("login without controlling tty", login, "missing\n", 1, NULL, 0) ||
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
          check("cp directory without -r", cp_directory_bad, NULL, 1, NULL, 0) ||
          check("ls recursive fixture", ls_tree, NULL, 0, "nested\n", 0) ||
          check("cp -r directory", cp_directory, NULL, 0, NULL, 0) ||
          (!is_directory("/tmp/coreutils_tree_copy/nested") && fail("cp -r nested directory")) ||
          (!file_equals("/tmp/coreutils_tree_copy/nested/file", "tree\n") && fail("cp -r nested file")) ||
          check("find directory", find_tree, NULL, 0, "/tmp/coreutils_tree/nested/file\n", 0) ||
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
          check("rm recursive", rm_recursive, NULL, 0, NULL, 0) ||
          check("rm -r tree", rm_tree, NULL, 0, NULL, 0) ||
          check("rm -r missing", rm_tree_bad, NULL, 1, NULL, 0) ||
          check("grep match", grep_match, "alpha\nbeta\ngamma\n", 0, NULL, 0) ||
          check("grep -n", grep_numbered, "alpha\nbeta\n", 0, "2:beta\n", 1) ||
          check("grep -i", grep_insensitive, "alpha\nBeta\ngamma\n", 0, NULL, 0) ||
          check("grep -i empty", grep_empty_insensitive, "\n", 0, "\n", 1) ||
          check("grep -v", grep_inverted, "alpha\nbeta\n", 0, NULL, 0) ||
         check("grep no match", grep_none, "alpha\nbeta\n", 1, NULL, 0) ||
         check("grep missing file", grep_missing, NULL, 2, NULL, 0) ||
         check("sort stdin", sort_stdin, "zulu\nalpha\n", 0, NULL, 0) ||
         check("cut fields", cut_fields, "one:two:three\n", 0, NULL, 0) ||
          check("tee", tee_file, "tee input\n", 0, NULL, 0) ||
          !file_equals("/tmp/coreutils_tee", "tee input\n") ||
          0)
        return 1;

    const char* help[] = {"cat", "head", "wc", "ls", "find", "pwd", "uname", "echo", "env",
                          "true", "false", "mkdir", "rm", "cp", "mv", "grep", "sort",
                           "cut", "tee", NULL};
    for (int i = 0; help[i] != NULL; ++i) {
        char* args[] = {(char*) help[i], "--help", NULL};
        if (check("--help", args, NULL, 0, "Usage: ", 0))
            return 1;
        args[1] = "--version";
        if (check(help[i], args, NULL, 0, "(Astia coreutils) 0.1\n", 0))
            return 1;
    }
    printf("coreutils tests ok\n");
    return 0;
}
