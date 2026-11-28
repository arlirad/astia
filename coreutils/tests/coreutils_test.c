#include <astia/version.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pty.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define UNAME_FIELD_MAX 64

static const char* uname_all_expected(void) {
    static char expected[2 * UNAME_FIELD_MAX + 32];

    char release[UNAME_FIELD_MAX + 1];
    char version[UNAME_FIELD_MAX + 1];

    snprintf(release, sizeof release, "%s", ASTIA_VERSION_STRING);
    snprintf(version, sizeof version, "Astia %s", ASTIA_VERSION_STRING);
    snprintf(expected, sizeof expected, "Astia astia %s %s x86_64\n", release, version);

    return expected;
}

static int fail(const char* what) {
    printf("coreutils test failed: %s\n", what);

    return 1;
}

static int uname_expected_is_current(void) {
    return strcmp(uname_all_expected(), "Astia astia 0.1 Astia kernel x86_64\n") != 0;
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

static int mode_is(const char* path, mode_t mode) {
    struct stat info;

    return stat(path, &info) == 0 && (info.st_mode & 0777) == mode;
}

static int is_directory(const char* path) {
    struct stat info;

    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static int is_symlink(const char* path) {
    struct stat info;

    return lstat(path, &info) == 0 && S_ISLNK(info.st_mode);
}

static int is_absent(const char* path) {
    struct stat info;

    return lstat(path, &info) != 0 && errno == ENOENT;
}

static int symlink_target_is(const char* path, const char* expected) {
    char buffer[256];

    long count = readlink(path, buffer, sizeof buffer - 1);
    if (count < 0)
        return 0;

    buffer[count] = '\0';

    return strcmp(buffer, expected) == 0;
}

static int same_file(const char* left, const char* right) {
    struct stat left_info;
    struct stat right_info;

    return stat(left, &left_info) == 0 && stat(right, &right_info) == 0 &&
           left_info.st_dev == right_info.st_dev && left_info.st_ino == right_info.st_ino;
}

static int has_mode(const char* path, mode_t mode) {
    struct stat info;

    return stat(path, &info) == 0 && (info.st_mode & 07777) == mode;
}

static int fork_cow_checks(void) {
    enum { page_count = 1024 };

    const size_t page_bytes = 4096;
    const size_t length     = (size_t) page_count * page_bytes;

    unsigned char* area =
        mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (area == MAP_FAILED)
        return fail("fork cow mmap");

    for (size_t page = 0; page < page_count; ++page) {
        area[page * page_bytes] = (unsigned char) (page * 31u + 7u);
    }

    pid_t child = fork();
    if (child < 0) {
        (void) munmap(area, length);

        return fail("fork cow fork");
    }

    if (child == 0) {
        for (size_t page = 0; page < page_count; ++page) {
            if (area[page * page_bytes] != (unsigned char) (page * 31u + 7u))
                _Exit(1);
        }

        for (size_t page = 0; page < page_count; ++page) {
            area[page * page_bytes] = (unsigned char) (page * 17u + 3u);
        }

        for (size_t page = 0; page < page_count; ++page) {
            if (area[page * page_bytes] != (unsigned char) (page * 17u + 3u))
                _Exit(2);
        }

        _Exit(0);
    }

    int status = 0;

    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0) {
        (void) munmap(area, length);

        return fail("fork cow child");
    }

    for (size_t page = 0; page < page_count; ++page) {
        if (area[page * page_bytes] != (unsigned char) (page * 31u + 7u)) {
            (void) munmap(area, length);

            return fail("fork cow parent");
        }
    }

    for (size_t page = 0; page < page_count; ++page) {
        area[page * page_bytes] ^= 0x5a;
    }

    for (size_t page = 0; page < page_count; ++page) {
        if (area[page * page_bytes] !=
            (unsigned char) (((unsigned char) (page * 31u + 7u)) ^ 0x5a)) {
            (void) munmap(area, length);

            return fail("fork cow parent write");
        }
    }

    if (munmap(area, length) != 0)
        return fail("fork cow munmap");

    return 0;
}

static int file_equals(const char* path, const char* expected) {
    FILE* file = fopen(path, "r");

    char   buffer[512];
    size_t length;
    size_t offset = 0;
    int    equal  = 1;

    if (file == NULL)
        return 0;

    length = strlen(expected);

    while (offset < length) {
        size_t chunk = length - offset;

        if (chunk > sizeof buffer)
            chunk = sizeof buffer;

        if (fread(buffer, 1, chunk, file) != chunk ||
            memcmp(buffer, expected + offset, chunk) != 0) {
            equal = 0;

            break;
        }

        offset += chunk;
    }

    if (equal && fgetc(file) != EOF)
        equal = 0;

    fclose(file);

    return equal;
}

static int credential_checks(void) {
    const char* file = "/tmp/coreutils_owner";
    const char* link = "/tmp/coreutils_owner_link";
    struct stat st;
    gid_t       groups[16];

    int count = getgroups(0, NULL);

    if (count < 0 || count > 16 || setgroups(-1, groups) != -1 || errno != EINVAL)
        return fail("groups validation");

    if (count != 0 && getgroups(count, groups) != count)
        return fail("getgroups");

    (void) unlink(file);
    (void) unlink(link);

    int fd = open(file, O_CREAT | O_RDWR, 0640);

    if (fd < 0 || fstat(fd, &st) != 0 || st.st_uid != getuid() || st.st_gid != getgid()) {
        if (fd >= 0)
            close(fd);

        return fail("ownership inheritance");
    }

    if (chown(file, getuid(), getgid()) != 0 || symlink(file, link) != 0 ||
        lchown(link, getuid(), getgid()) != 0 || lstat(link, &st) != 0 ||
        !S_ISLNK(st.st_mode))
        return fail("ownership APIs");

    close(fd);
    unlink(link);
    unlink(file);

    return 0;
}

static int account_checks(void) {
    struct passwd* root   = getpwnam("root");
    struct passwd* by_uid = getpwuid(0);
    struct group*  users  = getgrnam("users");

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
static int run_env(const char* name, char* const argv[], const char* input,
                   int expected_status, const char* expected_output, int exact,
                   char* const envp[]) {
    int saved_stdin = -1;

    if (input != NULL) {
        if (put_file("/tmp/coreutils_stdin", input) != 0)
            return -1;

        int fd = open("/tmp/coreutils_stdin", O_RDONLY);
        if (fd < 0)
            return -1;

        saved_stdin = dup(0);
        if (saved_stdin < 0) {
            close(fd);

            return -1;
        }

        if (dup2(fd, 0) < 0) {
            close(fd);
            close(saved_stdin);

            return -1;
        }

        close(fd);
    }

    char path[64];

    if (strlen(argv[0]) + 6 >= sizeof path) {
        if (saved_stdin >= 0) {
            (void) dup2(saved_stdin, 0);
            close(saved_stdin);
        }

        return -1;
    }

    snprintf(path, sizeof path, "/bin/%s", argv[0]);

    int saved_stdout = -1;
    int output_fd    = -1;

    if (expected_output != NULL) {
        saved_stdout = dup(1);
        output_fd    = open("/tmp/coreutils_stdout", O_WRONLY | O_CREAT | O_TRUNC, 0600);

        if (saved_stdout < 0 || output_fd < 0 || dup2(output_fd, 1) < 0) {
            if (output_fd >= 0)
                close(output_fd);

            if (saved_stdout >= 0) {
                (void) dup2(saved_stdout, 1);
                close(saved_stdout);
            }

            if (saved_stdin >= 0) {
                (void) dup2(saved_stdin, 0);
                close(saved_stdin);
            }

            return -1;
        }

        close(output_fd);
    }

    char* const default_envp[] = {"PATH=/bin", "COREUTILS_TEST=present", NULL};

    int child = spawnve(path, argv, envp == NULL ? default_envp : envp);
    if (child < 0) {
        if (saved_stdout >= 0)
            (void) dup2(saved_stdout, 1);

        if (saved_stdout >= 0)
            close(saved_stdout);

        if (saved_stdin >= 0) {
            (void) dup2(saved_stdin, 0);
            close(saved_stdin);
        }

        return -1;
    }

    int status = 0;
    int ok =
        waitpid(child, &status, 0) == child && ((status >> 8) & 255) == expected_status;

    if (saved_stdout >= 0) {
        if (dup2(saved_stdout, 1) < 0)
            ok = 0;

        close(saved_stdout);
    }

    if (saved_stdin >= 0) {
        if (dup2(saved_stdin, 0) < 0)
            ok = 0;

        close(saved_stdin);
    }

    if (!ok) {
        printf("run %s: status=%d\n", name, status >> 8 & 255);

        return -1;
    }

    if (expected_output != NULL &&
        !file_equals("/tmp/coreutils_stdout", expected_output)) {
        if (exact == 0) {
            FILE* output = fopen("/tmp/coreutils_stdout", "r");

            char actual[512] = {0};

            if (output == NULL || fread(actual, 1, sizeof actual - 1, output) == 0 ||
                strstr(actual, expected_output) == NULL) {
                if (output != NULL)
                    fclose(output);

                return -1;
            }

            fclose(output);
        }
        else {
            return -1;
        }
    }

    return 0;
}

static int run(const char* name, char* const argv[], const char* input,
               int expected_status, const char* expected_output, int exact) {
    return run_env(name, argv, input, expected_status, expected_output, exact, NULL);
}

static int read_all(int fd, char* buffer, size_t capacity) {
    size_t used = 0;

    while (used + 1 < capacity) {
        long count = read(fd, buffer + used, capacity - 1 - used);

        if (count < 0 && errno == EINTR)
            continue;

        if (count <= 0)
            break;

        used += (size_t) count;
    }

    buffer[used] = '\0';

    return 0;
}

static int is_kernel_log_line(const char* line, size_t length) {
    if (length == 0 || line[0] != '[')
        return 0;

    static const char* const markers[] = {" INFO] ", " WARN] ", "ERROR] ", "DEBUG] ",
                                          "PANIC] "};
    const char*              end       = line + length;

    for (size_t marker = 0; marker < sizeof markers / sizeof markers[0]; ++marker) {
        size_t marker_length = strlen(markers[marker]);

        for (const char* cursor = line; cursor + marker_length <= end; ++cursor) {
            if (memcmp(cursor, markers[marker], marker_length) == 0)
                return 1;
        }
    }

    return 0;
}

static void strip_kernel_log(char* text) {
    char* read  = text;
    char* write = text;

    while (*read != '\0') {
        char* newline = strchr(read, '\n');

        size_t length = newline != NULL ? (size_t) (newline - read) + 1 : strlen(read);

        if (!is_kernel_log_line(read, length)) {
            memmove(write, read, length);

            write += length;
        }

        read += length;
    }

    *write = '\0';
}

static int run_pty(const char* name, char* const argv[], char* const envp[],
                   unsigned short columns, const char* expected_output) {
    int master = -1;
    int slave  = -1;

    struct winsize window = {24, columns, 0, 0};

    char output[512];
    char path[64];

    if (openpty(&master, &slave, NULL, NULL, &window) != 0)
        return fail(name);

    struct termios settings;

    if (tcgetattr(slave, &settings) == 0) {
        settings.c_oflag &= ~OPOST;
        settings.c_lflag &= ~(ECHO | ICANON);

        (void) tcsetattr(slave, TCSANOW, &settings);
    }

    snprintf(path, sizeof path, "/bin/%s", argv[0]);

    int saved_stdout = dup(1);

    if (saved_stdout < 0 || dup2(slave, 1) < 0) {
        if (saved_stdout >= 0)
            close(saved_stdout);

        close(master);
        close(slave);

        return fail(name);
    }

    close(slave);

    int child = spawnve(path, argv, envp);

    (void) dup2(saved_stdout, 1);
    close(saved_stdout);

    if (child < 0) {
        close(master);

        return fail(name);
    }

    read_all(master, output, sizeof output);
    strip_kernel_log(output);

    int status = 0;
    int ok     = waitpid(child, &status, 0) == child && ((status >> 8) & 255) == 0;

    close(master);

    if (!ok || strcmp(output, expected_output) != 0) {
        printf("run %s: status=%d output=[%s]\n", name, status >> 8 & 255, output);

        return fail(name);
    }

    return 0;
}

static int check(const char* name, char* const argv[], const char* input, int status,
                 const char* output, int exact) {
    return run(name, argv, input, status, output, exact) == 0 ? 0 : fail(name);
}

static int check_env(const char* name, char* const argv[], char* const envp[], int status,
                     const char* output, int exact) {
    return run_env(name, argv, NULL, status, output, exact, envp) == 0 ? 0 : fail(name);
}

static int check_pty(const char* name, char* const argv[], char* const envp[],
                     unsigned short columns, const char* output) {
    return run_pty(name, argv, envp, columns, output) == 0 ? 0 : fail(name);
}

static void restore_descriptor(int target, int saved) {
    if (saved >= 0) {
        (void) dup2(saved, target);
        close(saved);
    }
}

/* login is the only utility here that reads from the controlling terminal.
 * Raw mode makes each write a separate read() instead of one canonical line,
 * so the username must be assembled across read calls. */
static int run_login_pty(const char* name, const char* first, const char* second) {
    int master = -1;
    int slave  = -1;

    struct winsize  window = {24, 80, 0, 0};
    struct timespec pause  = {1, 0};

    char output[1024];

    if (openpty(&master, &slave, NULL, NULL, &window) != 0)
        return fail(name);

    struct termios settings;

    if (tcgetattr(slave, &settings) != 0) {
        close(master);
        close(slave);

        return fail(name);
    }

    settings.c_lflag &= ~(ECHO | ICANON);
    settings.c_cc[VMIN]  = 1;
    settings.c_cc[VTIME] = 0;

    if (tcsetattr(slave, TCSANOW, &settings) != 0) {
        close(master);
        close(slave);

        return fail(name);
    }

    int saved_stdin  = dup(0);
    int saved_stdout = dup(1);
    int saved_stderr = dup(2);

    if (saved_stdin < 0 || saved_stdout < 0 || saved_stderr < 0 || dup2(slave, 0) < 0 ||
        dup2(slave, 1) < 0 || dup2(slave, 2) < 0) {
        restore_descriptor(0, saved_stdin);
        restore_descriptor(1, saved_stdout);
        restore_descriptor(2, saved_stderr);
        close(master);
        close(slave);

        return fail(name);
    }

    close(slave);

    char* login_argv[] = {"login", NULL};
    char* login_envp[] = {"PATH=/bin", NULL};

    int child = spawnve("/bin/login", login_argv, login_envp);

    restore_descriptor(0, saved_stdin);
    restore_descriptor(1, saved_stdout);
    restore_descriptor(2, saved_stderr);

    if (child < 0) {
        close(master);

        return fail(name);
    }

    if (write(master, first, strlen(first)) != (long) strlen(first)) {
        close(master);

        return fail(name);
    }

    (void) nanosleep(&pause, NULL);
    (void) write(master, second, strlen(second));

    read_all(master, output, sizeof output);

    int status = 0;
    int ok     = waitpid(child, &status, 0) == child && ((status >> 8) & 255) == 1;
    int passed = ok && strstr(output, "login: ") != NULL &&
                 strstr(output, "unknown local account") == NULL &&
                 strstr(output, "invalid username") == NULL;

    close(master);

    if (!passed)
        printf("run %s: status=%d output=[%s]\n", name, status >> 8 & 255, output);

    return passed ? 0 : fail(name);
}

static int login_pty_checks(void) {
    return run_login_pty("login split username", "ro", "ot\r");
}

static int check_output_excludes(const char* name, char* const argv[], int status,
                                 const char* present, const char* absent) {
    char buffer[1024] = {0};

    FILE* output;

    if (run(name, argv, NULL, status, present, 0) != 0)
        return fail(name);

    output = fopen("/tmp/coreutils_stdout", "r");
    if (output == NULL)
        return fail(name);

    size_t length = fread(buffer, 1, sizeof buffer - 1, output);

    fclose(output);

    buffer[length] = '\0';

    return strstr(buffer, absent) == NULL ? 0 : fail(name);
}

static int mv_directory_checks(void) {
    char* mv_into_directory[]           = {"mv", "/tmp/coreutils_mv_file",
                                           "/tmp/coreutils_mv_target", NULL};
    char* mv_directory_into_directory[] = {"mv", "/tmp/coreutils_mv_tree",
                                           "/tmp/coreutils_mv_target", NULL};

    /* The tmpfs at /tmp has a small fixed entry table, so this suite creates
       and removes its own fixtures instead of holding them for the whole run. */
    (void) unlink("/tmp/coreutils_mv_target/coreutils_mv_file");
    (void) unlink("/tmp/coreutils_mv_target/coreutils_mv_tree/nested/file");
    (void) rmdir("/tmp/coreutils_mv_target/coreutils_mv_tree/nested");
    (void) rmdir("/tmp/coreutils_mv_target/coreutils_mv_tree");
    (void) rmdir("/tmp/coreutils_mv_target");
    (void) unlink("/tmp/coreutils_mv_file");
    (void) unlink("/tmp/coreutils_mv_tree/nested/file");
    (void) rmdir("/tmp/coreutils_mv_tree/nested");
    (void) rmdir("/tmp/coreutils_mv_tree");

    if (put_file("/tmp/coreutils_mv_file", "move me\n") != 0 ||
        mkdir("/tmp/coreutils_mv_target", 0777) != 0 ||
        mkdir("/tmp/coreutils_mv_tree", 0777) != 0 ||
        mkdir("/tmp/coreutils_mv_tree/nested", 0777) != 0 ||
        put_file("/tmp/coreutils_mv_tree/nested/file", "mv tree\n") != 0)
        return fail("mv directory fixture setup");

    int status = 0;

    if (check("mv into directory", mv_into_directory, NULL, 0, NULL, 0) != 0 ||
        !is_file("/tmp/coreutils_mv_target/coreutils_mv_file") ||
        is_file("/tmp/coreutils_mv_file"))
        status = 1;

    if (check("mv directory into directory", mv_directory_into_directory, NULL, 0, NULL,
              0) != 0 ||
        !is_directory("/tmp/coreutils_mv_target/coreutils_mv_tree") ||
        is_directory("/tmp/coreutils_mv_tree") ||
        !file_equals("/tmp/coreutils_mv_target/coreutils_mv_tree/nested/file",
                     "mv tree\n"))
        status = 1;

    if (unlink("/tmp/coreutils_mv_target/coreutils_mv_file") != 0 ||
        unlink("/tmp/coreutils_mv_target/coreutils_mv_tree/nested/file") != 0 ||
        rmdir("/tmp/coreutils_mv_target/coreutils_mv_tree/nested") != 0 ||
        rmdir("/tmp/coreutils_mv_target/coreutils_mv_tree") != 0 ||
        rmdir("/tmp/coreutils_mv_target") != 0)
        status = 1;

    return status;
}

static int mv_cross_device_mode_checks(void) {
    char* mv_across[] = {"mv", "/tmp/coreutils_mv_mode", "/tmp/coreutils_mv_mnt/mode",
                         NULL};

    (void) unlink("/tmp/coreutils_mv_mnt/mode");
    (void) unlink("/tmp/coreutils_mv_mode");
    (void) rmdir("/tmp/coreutils_mv_mnt");

    if (mkdir("/tmp/coreutils_mv_mnt", 0700) != 0)
        return fail("mv cross-device mount point");

    if (mount("tmpfs", "/tmp/coreutils_mv_mnt", NULL) != 0) {
        (void) rmdir("/tmp/coreutils_mv_mnt");

        return fail("mv cross-device mount");
    }

    struct stat source_root;
    struct stat destination_root;

    int status = 0;

    if (stat("/tmp", &source_root) != 0 ||
        stat("/tmp/coreutils_mv_mnt", &destination_root) != 0 ||
        source_root.st_dev == destination_root.st_dev ||
        put_file("/tmp/coreutils_mv_mode", "mode\n") != 0 ||
        chmod("/tmp/coreutils_mv_mode", 0640) != 0) {
        status = 1;
    }
    else {
        /* A permissive umask must neither widen nor narrow the source mode. */
        mode_t saved_umask = umask(0077);

        int moved = check("mv cross-device mode", mv_across, NULL, 0, NULL, 0);

        (void) umask(saved_umask);

        if (moved != 0 || is_file("/tmp/coreutils_mv_mode") ||
            !file_equals("/tmp/coreutils_mv_mnt/mode", "mode\n") ||
            !has_mode("/tmp/coreutils_mv_mnt/mode", 0640))
            status = 1;
    }

    if (unlink("/tmp/coreutils_mv_mnt/mode") != 0 && errno != ENOENT)
        status = 1;

    if (unlink("/tmp/coreutils_mv_mode") != 0 && errno != ENOENT)
        status = 1;

    if (umount("/tmp/coreutils_mv_mnt") != 0 || rmdir("/tmp/coreutils_mv_mnt") != 0)
        status = 1;

    return status == 0 ? 0 : fail("mv cross-device mode");
}

static int mv_cross_device_symlink_checks(void) {
    char* mv_relative[] = {"mv", "/tmp/coreutils_mv_symlink",
                           "/tmp/coreutils_mv_symlink_mnt/link", NULL};
    char* mv_dangling[] = {"mv", "/tmp/coreutils_mv_dangling_symlink",
                           "/tmp/coreutils_mv_symlink_mnt/dangling", NULL};

    (void) unlink("/tmp/coreutils_mv_symlink_mnt/link");
    (void) unlink("/tmp/coreutils_mv_symlink_mnt/dangling");
    (void) unlink("/tmp/coreutils_mv_symlink");
    (void) unlink("/tmp/coreutils_mv_dangling_symlink");
    (void) rmdir("/tmp/coreutils_mv_symlink_mnt");

    if (mkdir("/tmp/coreutils_mv_symlink_mnt", 0700) != 0)
        return fail("mv cross-device symlink mount point");

    if (mount("tmpfs", "/tmp/coreutils_mv_symlink_mnt", NULL) != 0) {
        (void) rmdir("/tmp/coreutils_mv_symlink_mnt");

        return fail("mv cross-device symlink mount");
    }

    struct stat source_root;
    struct stat destination_root;

    int status = 0;

    if (stat("/tmp", &source_root) != 0 ||
        stat("/tmp/coreutils_mv_symlink_mnt", &destination_root) != 0 ||
        source_root.st_dev == destination_root.st_dev ||
        symlink("relative/target", "/tmp/coreutils_mv_symlink") != 0 ||
        symlink("/nonexistent/astia-669", "/tmp/coreutils_mv_dangling_symlink") != 0) {
        status = 1;
    }
    else {
        if (check("mv cross-device relative symlink", mv_relative, NULL, 0, NULL, 0) !=
                0 ||
            !is_symlink("/tmp/coreutils_mv_symlink_mnt/link") ||
            !symlink_target_is("/tmp/coreutils_mv_symlink_mnt/link", "relative/target") ||
            !is_absent("/tmp/coreutils_mv_symlink"))
            status = 1;

        if (check("mv cross-device dangling symlink", mv_dangling, NULL, 0, NULL, 0) !=
                0 ||
            !is_symlink("/tmp/coreutils_mv_symlink_mnt/dangling") ||
            !symlink_target_is("/tmp/coreutils_mv_symlink_mnt/dangling",
                               "/nonexistent/astia-669") ||
            !is_absent("/tmp/coreutils_mv_dangling_symlink"))
            status = 1;
    }

    if (unlink("/tmp/coreutils_mv_symlink_mnt/link") != 0 && errno != ENOENT)
        status = 1;

    if (unlink("/tmp/coreutils_mv_symlink_mnt/dangling") != 0 && errno != ENOENT)
        status = 1;

    if (unlink("/tmp/coreutils_mv_symlink") != 0 && errno != ENOENT)
        status = 1;

    if (unlink("/tmp/coreutils_mv_dangling_symlink") != 0 && errno != ENOENT)
        status = 1;

    if (umount("/tmp/coreutils_mv_symlink_mnt") != 0 ||
        rmdir("/tmp/coreutils_mv_symlink_mnt") != 0)
        status = 1;

    return status == 0 ? 0 : fail("mv cross-device symlink");
}

static int ln_checks(void) {
    char* ln_hard[]       = {"ln", "/tmp/coreutils_a", "/tmp/coreutils_hard", NULL};
    char* ln_force_hard[] = {"ln", "-f", "/tmp/coreutils_b", "/tmp/coreutils_hard", NULL};
    char* ln_symbolic[]   = {"ln", "-s", "coreutils_a", "/tmp/coreutils_link", NULL};
    char* ln_symbolic_long[]  = {"ln", "--symbolic", "/tmp/coreutils_a",
                                 "/tmp/coreutils_symlink_long", NULL};
    char* ln_force_symbolic[] = {"ln", "-sf", "/tmp/coreutils_a", "/tmp/coreutils_link",
                                 NULL};
    char* ln_relative_force[] = {"ln", "-sf", "coreutils_a", "/tmp/coreutils_link", NULL};
    char* ln_missing[]        = {"ln", "/tmp/coreutils_missing",
                                 "/tmp/coreutils_symlink_missing", NULL};
    char* readlink_link[]     = {"readlink", "/tmp/coreutils_link", NULL};

    (void) unlink("/tmp/coreutils_hard");
    (void) unlink("/tmp/coreutils_link");
    (void) unlink("/tmp/coreutils_symlink_long");
    (void) unlink("/tmp/coreutils_symlink_missing");

    int status = 0;

    if (check("ln hard link", ln_hard, NULL, 0, NULL, 0) != 0 ||
        !same_file("/tmp/coreutils_a", "/tmp/coreutils_hard") ||
        is_symlink("/tmp/coreutils_hard") ||
        check("ln existing target", ln_hard, NULL, 1, NULL, 0) != 0 ||
        !same_file("/tmp/coreutils_a", "/tmp/coreutils_hard") ||
        check("ln force hard link", ln_force_hard, NULL, 0, NULL, 0) != 0 ||
        !same_file("/tmp/coreutils_b", "/tmp/coreutils_hard") ||
        same_file("/tmp/coreutils_a", "/tmp/coreutils_hard") ||
        check("ln symbolic", ln_symbolic, NULL, 0, NULL, 0) != 0 ||
        !is_symlink("/tmp/coreutils_link") ||
        check("readlink", readlink_link, NULL, 0, "coreutils_a\n", 1) != 0 ||
        check("ln -sf symbolic", ln_force_symbolic, NULL, 0, NULL, 0) != 0 ||
        !is_symlink("/tmp/coreutils_link") ||
        check("readlink replaced symlink", readlink_link, NULL, 0, "/tmp/coreutils_a\n",
              1) != 0 ||
        check("ln --symbolic", ln_symbolic_long, NULL, 0, NULL, 0) != 0 ||
        !is_symlink("/tmp/coreutils_symlink_long") ||
        check("ln missing target", ln_missing, NULL, 1, NULL, 0) != 0 ||
        is_symlink("/tmp/coreutils_symlink_missing") ||
        check("ln -sf relative", ln_relative_force, NULL, 0, NULL, 0) != 0 ||
        !is_symlink("/tmp/coreutils_link") ||
        check("readlink restored symlink", readlink_link, NULL, 0, "coreutils_a\n", 1) !=
            0)
        status = 1;

    (void) unlink("/tmp/coreutils_hard");
    (void) unlink("/tmp/coreutils_symlink_long");
    (void) unlink("/tmp/coreutils_symlink_missing");

    return status;
}

static int ls_symlink_checks(void) {
    char* ls_symlink_file[]     = {"ls", "/tmp/coreutils_link", NULL};
    char* ls_symlink_dir[]      = {"ls", "/tmp/coreutils_ls_link", NULL};
    char* ls_symlink_dir_long[] = {"ls", "-l", "/tmp/coreutils_ls_link", NULL};
    char* ls_long[]             = {"ls", "-l", "/tmp/coreutils_link", NULL};

    (void) unlink("/tmp/coreutils_ls_link");
    (void) unlink("/tmp/coreutils_ls_target/sample");
    (void) rmdir("/tmp/coreutils_ls_target");

    if (mkdir("/tmp/coreutils_ls_target", 0777) != 0 ||
        put_file("/tmp/coreutils_ls_target/sample", "x\n") != 0 ||
        symlink("/tmp/coreutils_ls_target", "/tmp/coreutils_ls_link") != 0)
        return fail("ls symlink fixture setup");

    int status = 0;

    if (check("ls symlink to file", ls_symlink_file, NULL, 0, "coreutils_link\n", 1) !=
            0 ||
        check("ls -l symlink", ls_long, NULL, 0, " -> coreutils_a\n", 0) != 0 ||
        check("ls symlink to directory", ls_symlink_dir, NULL, 0, "sample\n", 1) != 0 ||
        check("ls -l symlink to directory", ls_symlink_dir_long, NULL, 0,
              " -> /tmp/coreutils_ls_target\n", 0) != 0)
        status = 1;

    (void) unlink("/tmp/coreutils_ls_link");
    (void) unlink("/tmp/coreutils_ls_target/sample");
    (void) rmdir("/tmp/coreutils_ls_target");

    return status;
}

static int ls_hidden_checks(void) {
    char* ls_hidden_default[]  = {"ls", "/tmp/coreutils_ls_col", NULL};
    char* ls_hidden_all[]      = {"ls", "-a", "/tmp/coreutils_ls_col", NULL};
    char* ls_hidden_almost[]   = {"ls", "-A", "/tmp/coreutils_ls_col", NULL};
    char* ls_hidden_long_all[] = {"ls", "-la", "/tmp/coreutils_ls_col", NULL};

    (void) unlink("/tmp/coreutils_ls_col/.hidden");

    if (put_file("/tmp/coreutils_ls_col/.hidden", "x\n") != 0)
        return fail("ls hidden fixture setup");

    int status = 0;

    if (check("ls hides dotfiles by default", ls_hidden_default, NULL, 0,
              "alpha\nbeta\ndelta\ngamma\n", 1) != 0 ||
        check("ls -a lists dotfiles and dot entries", ls_hidden_all, NULL, 0,
              ".\n..\n.hidden\nalpha\nbeta\ndelta\ngamma\n", 1) != 0 ||
        check("ls -A lists dotfiles without dot entries", ls_hidden_almost, NULL, 0,
              ".hidden\nalpha\nbeta\ndelta\ngamma\n", 1) != 0 ||
        check("ls -la combined flags dot entry", ls_hidden_long_all, NULL, 0, " .\n",
              0) != 0 ||
        check("ls -la combined flags hidden entry", ls_hidden_long_all, NULL, 0,
              " .hidden\n", 0) != 0)
        status = 1;

    (void) unlink("/tmp/coreutils_ls_col/.hidden");

    return status;
}

static int rm_directory_checks(void) {
    char* rm_multi[] = {"rm", "-r", "/tmp/coreutils_rm_multi", NULL};

    (void) unlink("/tmp/coreutils_rm_multi/one");
    (void) unlink("/tmp/coreutils_rm_multi/two");
    (void) unlink("/tmp/coreutils_rm_multi/nested/alpha");
    (void) unlink("/tmp/coreutils_rm_multi/nested/beta");
    (void) rmdir("/tmp/coreutils_rm_multi/nested");
    (void) rmdir("/tmp/coreutils_rm_multi");

    if (mkdir("/tmp/coreutils_rm_multi", 0777) != 0 ||
        put_file("/tmp/coreutils_rm_multi/one", "one\n") != 0 ||
        put_file("/tmp/coreutils_rm_multi/two", "two\n") != 0 ||
        mkdir("/tmp/coreutils_rm_multi/nested", 0777) != 0 ||
        put_file("/tmp/coreutils_rm_multi/nested/alpha", "alpha\n") != 0 ||
        put_file("/tmp/coreutils_rm_multi/nested/beta", "beta\n") != 0)
        return fail("rm multi-child fixture setup");

    if (check("rm -r multiple children", rm_multi, NULL, 0, NULL, 0) != 0 ||
        is_directory("/tmp/coreutils_rm_multi") || is_file("/tmp/coreutils_rm_multi/one"))
        return 1;

    return 0;
}

static int find_symlink_checks(void) {
    char* find_tree[]     = {"find", "/tmp/coreutils_find", NULL};
    char* find_follow[]   = {"find", "-L", "/tmp/coreutils_find", NULL};
    char* find_top_link[] = {"find", "-H", "/tmp/coreutils_find/tree/link", NULL};

    (void) unlink("/tmp/coreutils_find/tree/self");
    (void) unlink("/tmp/coreutils_find/tree/link");
    (void) unlink("/tmp/coreutils_find/tree/file");
    (void) rmdir("/tmp/coreutils_find/tree");
    (void) unlink("/tmp/coreutils_find/real/file");
    (void) rmdir("/tmp/coreutils_find/real");
    (void) rmdir("/tmp/coreutils_find");

    if (mkdir("/tmp/coreutils_find", 0777) != 0 ||
        mkdir("/tmp/coreutils_find/real", 0777) != 0 ||
        put_file("/tmp/coreutils_find/real/file", "target\n") != 0 ||
        mkdir("/tmp/coreutils_find/tree", 0777) != 0 ||
        put_file("/tmp/coreutils_find/tree/file", "tree\n") != 0 ||
        symlink("../real", "/tmp/coreutils_find/tree/link") != 0 ||
        symlink(".", "/tmp/coreutils_find/tree/self") != 0)
        return fail("find symlink fixture setup");

    int status = 0;

    if (check("find symlink not followed", find_tree, NULL, 0,
              "/tmp/coreutils_find/tree/link\n", 0) != 0 ||
        check_output_excludes("find symlink not descended", find_tree, 0,
                              "/tmp/coreutils_find/real/file\n",
                              "/tmp/coreutils_find/tree/link/file\n") != 0 ||
        check("find self symlink printed", find_tree, NULL, 0,
              "/tmp/coreutils_find/tree/self\n", 0) != 0 ||
        check("find -L follows and breaks cycles", find_follow, NULL, 1,
              "/tmp/coreutils_find/tree/link/file\n", 0) != 0 ||
        check("find -H follows command-line symlink", find_top_link, NULL, 0,
              "/tmp/coreutils_find/tree/link/file\n", 0) != 0)
        status = 1;

    (void) unlink("/tmp/coreutils_find/tree/self");
    (void) unlink("/tmp/coreutils_find/tree/link");
    (void) unlink("/tmp/coreutils_find/tree/file");
    (void) rmdir("/tmp/coreutils_find/tree");
    (void) unlink("/tmp/coreutils_find/real/file");
    (void) rmdir("/tmp/coreutils_find/real");
    (void) rmdir("/tmp/coreutils_find");

    return status;
}

static int tree_walk_checks(void) {
    char* find_walk[]  = {"find", "/tmp/coreutils_walk", NULL};
    char* cp_walk[]    = {"cp", "-r", "/tmp/coreutils_walk", "/tmp/coreutils_walk_copy",
                          NULL};
    char* chmod_walk[] = {"chmod", "-R", "640", "/tmp/coreutils_walk", NULL};
    char* rm_walk[]    = {"rm", "-r", "/tmp/coreutils_walk", "/tmp/coreutils_walk_copy",
                          NULL};

    (void) unlink("/tmp/coreutils_b");
    (void) unlink("/tmp/coreutils_lines");
    (void) unlink("/tmp/coreutils_mode");
    (void) unlink("/tmp/coreutils_touch");
    (void) unlink("/tmp/coreutils_stdin");
    (void) unlink("/tmp/coreutils_from_stdin");
    (void) unlink("/tmp/coreutils_tee");
    (void) unlink("/tmp/coreutils_long_sort");
    (void) unlink("/tmp/coreutils_tree/nested/file");
    (void) rmdir("/tmp/coreutils_tree/nested");
    (void) rmdir("/tmp/coreutils_tree");
    (void) unlink("/tmp/coreutils_ls_col/gamma");
    (void) unlink("/tmp/coreutils_ls_col/alpha");
    (void) unlink("/tmp/coreutils_ls_col/delta");
    (void) unlink("/tmp/coreutils_ls_col/beta");
    (void) rmdir("/tmp/coreutils_ls_col");
    (void) rmdir("/tmp/coreutils_ls_color/sub");
    (void) rmdir("/tmp/coreutils_ls_color");
    (void) unlink("/tmp/coreutils_ls_wide/abcdefghijklmnopqrstuvwxyz0123");
    (void) rmdir("/tmp/coreutils_ls_wide");
    (void) rmdir("/tmp/coreutils_ls_empty");
    (void) rmdir("/tmp/coreutils_parent/child");
    (void) rmdir("/tmp/coreutils_parent");

    (void) unlink("/tmp/coreutils_walk/a/b/c/file");
    (void) rmdir("/tmp/coreutils_walk/a/b/c");
    (void) rmdir("/tmp/coreutils_walk/a/b");
    (void) unlink("/tmp/coreutils_walk/a/g");
    (void) rmdir("/tmp/coreutils_walk/a");
    (void) unlink("/tmp/coreutils_walk/h/i");
    (void) rmdir("/tmp/coreutils_walk/h");
    (void) rmdir("/tmp/coreutils_walk");
    (void) unlink("/tmp/coreutils_walk_copy/a/b/c/file");
    (void) rmdir("/tmp/coreutils_walk_copy/a/b/c");
    (void) rmdir("/tmp/coreutils_walk_copy/a/b");
    (void) unlink("/tmp/coreutils_walk_copy/a/g");
    (void) rmdir("/tmp/coreutils_walk_copy/a");
    (void) unlink("/tmp/coreutils_walk_copy/h/i");
    (void) rmdir("/tmp/coreutils_walk_copy/h");
    (void) rmdir("/tmp/coreutils_walk_copy");

    if (mkdir("/tmp/coreutils_walk", 0777) != 0 ||
        mkdir("/tmp/coreutils_walk/a", 0777) != 0 ||
        mkdir("/tmp/coreutils_walk/a/b", 0777) != 0 ||
        mkdir("/tmp/coreutils_walk/a/b/c", 0777) != 0 ||
        put_file("/tmp/coreutils_walk/a/b/c/file", "deep\n") != 0 ||
        put_file("/tmp/coreutils_walk/a/g", "side\n") != 0 ||
        mkdir("/tmp/coreutils_walk/h", 0777) != 0 ||
        put_file("/tmp/coreutils_walk/h/i", "other\n") != 0)
        return fail("tree walk fixture setup");

    int status = 0;

    if (check("find nested order", find_walk, NULL, 0,
              "/tmp/coreutils_walk\n"
              "/tmp/coreutils_walk/a\n"
              "/tmp/coreutils_walk/a/b\n"
              "/tmp/coreutils_walk/a/b/c\n"
              "/tmp/coreutils_walk/a/b/c/file\n"
              "/tmp/coreutils_walk/a/g\n"
              "/tmp/coreutils_walk/h\n"
              "/tmp/coreutils_walk/h/i\n",
              1) != 0 ||
        check("cp -r nested tree", cp_walk, NULL, 0, NULL, 0) != 0 ||
        !file_equals("/tmp/coreutils_walk_copy/a/b/c/file", "deep\n") ||
        !file_equals("/tmp/coreutils_walk_copy/a/g", "side\n") ||
        !file_equals("/tmp/coreutils_walk_copy/h/i", "other\n") ||
        check("chmod -R nested tree", chmod_walk, NULL, 0, NULL, 0) != 0 ||
        !has_mode("/tmp/coreutils_walk/a/b/c", 0640) ||
        !has_mode("/tmp/coreutils_walk/a/b/c/file", 0640) ||
        !has_mode("/tmp/coreutils_walk/h/i", 0640) ||
        check("rm -r nested tree", rm_walk, NULL, 0, NULL, 0) != 0 ||
        is_directory("/tmp/coreutils_walk") || is_directory("/tmp/coreutils_walk_copy"))
        status = 1;

    return status;
}

static int id_checks(void) {
    char* id[]         = {"id", NULL};
    char* id_help[]    = {"id", "--help", NULL};
    char* id_version[] = {"id", "--version", NULL};
    gid_t groups[2]    = {1000, 10};

    if (check("id newline", id, NULL, 0, "uid=0(root) gid=0(root) groups=0(root)\n", 1) !=
        0)
        return 1;

    if (setgroups(2, groups) != 0)
        return fail("id supplementary setup");

    int rc = check("id groups newline", id, NULL, 0,
                   "uid=0(root) gid=0(root) groups=0(root),1000(users),10(10)\n", 1);

    if (setgroups(0, NULL) != 0)
        return fail("id supplementary restore");

    if (rc != 0)
        return 1;

    if (check("id help newline", id_help, NULL, 0,
              "Usage: id\n\nPrint the effective UID, GID, and supplementary groups.\n",
              1) != 0)
        return 1;

    return check("id version newline", id_version, NULL, 0, "id (Astia coreutils) 0.1\n",
                 1);
}

#define LONG_LINE_PADDING 5000

static int sort_empty_checks(void) {
    char* sort_stdin[]      = {"sort", NULL};
    char* sort_empty_file[] = {"sort", "/tmp/coreutils_empty", NULL};

    (void) unlink("/tmp/coreutils_empty");

    if (put_file("/tmp/coreutils_empty", "") != 0)
        return fail("sort empty fixture setup");

    int status = check("sort empty stdin", sort_stdin, "", 0, "", 1) ||
                 check("sort empty file", sort_empty_file, NULL, 0, "", 1);

    (void) unlink("/tmp/coreutils_empty");

    return status;
}

static int long_line_checks(void) {
    static char grep_line[LONG_LINE_PADDING + 64];
    static char cut_line[LONG_LINE_PADDING + 64];
    static char sort_first[LONG_LINE_PADDING + 16];
    static char sort_second[LONG_LINE_PADDING + 16];
    static char head_line[LONG_LINE_PADDING + 2];
    static char head_fixture[LONG_LINE_PADDING + 8];

    static char sort_expected[2 * (LONG_LINE_PADDING + 16) + 1];
    static char sort_input[2 * (LONG_LINE_PADDING + 16) + 1];

    char* grep_boundary[]  = {"grep", "cross-boundary", NULL};
    char* cut_late_field[] = {"cut", "-d", ":", "-f", "2", NULL};
    char* sort_long[]      = {"sort", NULL};
    char* sort_unique[]    = {"sort", "-u", "/tmp/coreutils_long_sort", NULL};
    char* head_long[]      = {"head", "-n", "1", "/tmp/coreutils_long_head", NULL};

    memset(grep_line, 'x', 4090);
    memcpy(grep_line + 4090, "cross-boundary", 14);
    memset(grep_line + 4104, 'y', LONG_LINE_PADDING - 4104);

    grep_line[LONG_LINE_PADDING]     = '\n';
    grep_line[LONG_LINE_PADDING + 1] = '\0';

    memset(cut_line, 'a', LONG_LINE_PADDING);

    cut_line[LONG_LINE_PADDING] = ':';

    memcpy(cut_line + LONG_LINE_PADDING + 1, "tail-field\n", 12);

    memset(sort_first, 'a', LONG_LINE_PADDING);
    memcpy(sort_first + LONG_LINE_PADDING, "a\n", 3);
    memset(sort_second, 'a', LONG_LINE_PADDING);
    memcpy(sort_second + LONG_LINE_PADDING, "b\n", 3);

    size_t first_length  = strlen(sort_first);
    size_t second_length = strlen(sort_second);

    memcpy(sort_expected, sort_first, first_length);
    memcpy(sort_expected + first_length, sort_second, second_length + 1);
    memcpy(sort_input, sort_second, second_length);
    memcpy(sort_input + second_length, sort_first, first_length + 1);

    if (put_file("/tmp/coreutils_long_sort", sort_expected) != 0)
        return fail("long line sort fixture");

    memset(head_line, 'h', LONG_LINE_PADDING);

    head_line[LONG_LINE_PADDING]     = '\n';
    head_line[LONG_LINE_PADDING + 1] = '\0';

    memset(head_fixture, 'h', LONG_LINE_PADDING);
    memcpy(head_fixture + LONG_LINE_PADDING, "\ntail\n", 7);

    if (put_file("/tmp/coreutils_long_head", head_fixture) != 0)
        return fail("long line head fixture");

    int status =
        check("grep line across 4095", grep_boundary, grep_line, 0, grep_line, 1) ||
        check("cut field after 4095", cut_late_field, cut_line, 0, "tail-field\n", 1) ||
        check("sort long lines", sort_long, sort_input, 0, sort_expected, 1) ||
        check("sort -u long lines", sort_unique, NULL, 0, sort_expected, 1) ||
        check("head long line", head_long, NULL, 0, head_line, 1);

    (void) unlink("/tmp/coreutils_long_head");
    (void) unlink("/tmp/coreutils_long_sort");

    return status;
}

#define SORT_GROWTH_LINES 300

static int sort_growth_checks(void) {
    static char input[SORT_GROWTH_LINES * 12 + 1];
    static char expected[SORT_GROWTH_LINES * 12 + 1];

    char* sort_many[] = {"sort", NULL};

    size_t input_length    = 0;
    size_t expected_length = 0;

    for (int index = SORT_GROWTH_LINES - 1; index >= 0; --index) {
        input_length += (size_t) snprintf(
            input + input_length, sizeof input - input_length, "line-%03d\n", index);
    }

    for (int index = 0; index < SORT_GROWTH_LINES; ++index) {
        expected_length +=
            (size_t) snprintf(expected + expected_length,
                              sizeof expected - expected_length, "line-%03d\n", index);
    }

    return check("sort many lines", sort_many, input, 0, expected, 1);
}

static int head_header_checks(void) {
    char* head_pair[]  = {"head", "/tmp/coreutils_head_a", "/tmp/coreutils_head_b", NULL};
    char* head_quiet[] = {"head", "-q", "/tmp/coreutils_head_a", "/tmp/coreutils_head_b",
                          NULL};
    char* head_verbose[] = {"head", "-v", "/tmp/coreutils_head_a", NULL};
    char* head_stdin[]   = {"head", "-v", NULL};
    char* head_dash[]    = {"head", "-", "/tmp/coreutils_head_a", NULL};
    char* head_single[]  = {"head", "/tmp/coreutils_head_a", NULL};

    if (put_file("/tmp/coreutils_head_a", "a\n") != 0 ||
        put_file("/tmp/coreutils_head_b", "b\n") != 0)
        return fail("head header fixtures");

    int status =
        check("head multiple files", head_pair, NULL, 0,
              "==> /tmp/coreutils_head_a <==\na\n\n"
              "==> /tmp/coreutils_head_b <==\nb\n",
              1) ||
        check("head -q", head_quiet, NULL, 0, "a\nb\n", 1) ||
        check("head -v", head_verbose, NULL, 0, "==> /tmp/coreutils_head_a <==\na\n",
              1) ||
        check("head -v stdin", head_stdin, "x\n", 0, "==> standard input <==\nx\n", 1) ||
        check("head dash operand", head_dash, "x\n", 0,
              "==> standard input <==\nx\n\n==> /tmp/coreutils_head_a <==\na\n", 1) ||
        check("head single file", head_single, NULL, 0, "a\n", 1);

    (void) unlink("/tmp/coreutils_head_a");
    (void) unlink("/tmp/coreutils_head_b");

    return status;
}

int main(void) {
    if (!uname_expected_is_current())
        return fail("uname expected output stale");

    if (credential_checks() != 0)
        return 1;

    if (account_checks() != 0)
        return 1;

    if (id_checks() != 0)
        return 1;

    if (chdir("/bin") != 0)
        return fail("runner chdir");

    if (fork_cow_checks() != 0)
        return 1;

    char* cat_stdin[]       = {"cat", NULL};
    char* cat_stdin_dash[]  = {"cat", "-", NULL};
    char* cat_file_stdin[]  = {"cat", "/tmp/coreutils_a", "-", NULL};
    char* cat_files[]       = {"cat", "/tmp/coreutils_a", "/tmp/coreutils_b", NULL};
    char* cat_missing[]     = {"cat", "/tmp/coreutils_missing", NULL};
    char* head_lines[]      = {"head", "-n", "2", "/tmp/coreutils_lines", NULL};
    char* head_bad[]        = {"head", "-n", NULL};
    char* head_bad_text[]   = {"head", "-n", "not-a-number", NULL};
    char* wc_files[]        = {"wc", "/tmp/coreutils_a", "/tmp/coreutils_b", NULL};
    char* wc_bad[]          = {"wc", "--bad", NULL};
    char* ls_dir[]          = {"ls", "/bin", NULL};
    char* ls_multi[]        = {"ls", "/bin", "/sbin", NULL};
    char* ls_two_dirs[]     = {"ls", "/tmp/coreutils_ls_col", "/tmp/coreutils_ls_color",
                               NULL};
    char* ls_file_dir[]     = {"ls", "/tmp/coreutils_a", "/tmp/coreutils_ls_empty", NULL};
    char* ls_bad[]          = {"ls", "--bad", NULL};
    char* pwd[]             = {"pwd", NULL};
    char* pwd_bad[]         = {"pwd", "extra", NULL};
    char* echo[]            = {"echo", "hello", "Astia", NULL};
    char* echo_n[]          = {"echo", "-n", "hello", NULL};
    char* env[]             = {"env", NULL};
    char* env_bad[]         = {"env", "extra", NULL};
    char* env_sorted[]      = {"env", "-s", NULL};
    char* env_sorted_long[] = {"env", "--sorted", NULL};
    char* env_assign[]      = {"env", "A=1", "B=2", "A=3", NULL};
    char* env_assign_sorted[] = {"env", "-s", "A=1", "B=2", "A=3", NULL};
    char* env_invalid_name[]  = {"env", "=x", NULL};
    char* env_exec[]          = {"env", "PATH=/bin", "true", NULL};
    char* env_empty[]         = {NULL};
    char* env_known[]         = {"Z=1", "A=2", "M=3", NULL};
    char* env_duplicates[]    = {"B=2", "A=1", "B=3", NULL};
    char* uname[]             = {"uname", NULL};
    char* uname_all[]         = {"uname", "-a", NULL};
    char* uname_bad[]         = {"uname", "--bad", NULL};

    char* uname_all_machine[]     = {"uname", "-a", "-m", NULL};
    char* uname_machine_all[]     = {"uname", "-m", "-a", NULL};
    char* uname_machine_sysname[] = {"uname", "-m", "-s", NULL};
    char* true_args[]             = {"true", "ignored", NULL};
    char* false_args[]            = {"false", "ignored", NULL};
    char* poweroff_bad[]          = {"poweroff", "--bad", NULL};
    char* reboot_bad[]            = {"reboot", "--bad", NULL};
    char* mkdir_one[]             = {"mkdir", "/tmp/coreutils_dir", NULL};
    char* mkdir_parents[]         = {"mkdir", "-p", "/tmp/coreutils_parent/child", NULL};
    char* mkdir_existing[]        = {"mkdir", "-p", "/tmp/coreutils_parent/child", NULL};
    char* mkdir_bad[]         = {"mkdir", "/tmp/coreutils_parent/missing/child", NULL};
    char* mkdir_file_parent[] = {"mkdir", "-p", "/tmp/coreutils_a/child", NULL};
    char* touch_create[]      = {"touch", "/tmp/coreutils_touch", NULL};
    char* touch_no_create[]   = {"touch", "-c", "/tmp/coreutils_touch_missing", NULL};
    char* touch_access_only[] = {"touch", "-a", "/tmp/coreutils_touch", NULL};
    char* touch_modify[]      = {"touch", "-m", "/tmp/coreutils_touch", NULL};
    char* touch_both[]        = {"touch", "-a", "-m", "/tmp/coreutils_touch", NULL};
    char* cp_file[]           = {"cp", "/tmp/coreutils_a", "/tmp/coreutils_copy", NULL};
    char* cp_mode[] = {"cp", "/tmp/coreutils_mode_src", "/tmp/coreutils_mode_copy", NULL};
    char* cp_self[] = {"cp", "/tmp/coreutils_a", "/tmp/coreutils_a", NULL};
    char* cp_hardlink[]  = {"cp", "/tmp/coreutils_a", "/tmp/coreutils_cp_hard", NULL};
    char* cp_symlink[]   = {"cp", "/tmp/coreutils_a", "/tmp/coreutils_link", NULL};
    char* cp_directory[] = {"cp", "-r", "/tmp/coreutils_tree", "/tmp/coreutils_tree_copy",
                            NULL};
    char* cp_directory_bad[]  = {"cp", "/tmp/coreutils_tree", "/tmp/coreutils_tree_bad",
                                 NULL};
    char* cp_recursive_self[] = {"cp", "-r", "/tmp/coreutils_tree", "/tmp/coreutils_tree",
                                 NULL};
    char* cp_recursive_inside[] = {"cp", "-r", "/tmp/coreutils_tree",
                                   "/tmp/coreutils_tree/nested_copy", NULL};
    char* cp_stdin[]            = {"cp", "-", "/tmp/coreutils_from_stdin", NULL};
    char* cp_stdin_self[]       = {"cp", "-", "/tmp/coreutils_stdin", NULL};
    char* mv_file[]    = {"mv", "/tmp/coreutils_copy", "/tmp/coreutils_moved", NULL};
    char* mv_replace[] = {"mv", "/tmp/coreutils_a", "/tmp/coreutils_moved", NULL};
    char* mv_missing[] = {"mv", "/tmp/coreutils_missing", "/tmp/coreutils_moved", NULL};
    char* rm_file[]    = {"rm", "/tmp/coreutils_moved", NULL};
    char* rm_missing[] = {"rm", "/tmp/coreutils_moved", NULL};
    char* rm_force[]   = {"rm", "-f", "/tmp/coreutils_moved", NULL};
    char* rm_force_empty[]       = {"rm", "-f", NULL};
    char* rm_dangling[]          = {"rm", "/tmp/coreutils_dangling", NULL};
    char* rm_recursive[]         = {"rm", "-r", "/tmp/coreutils_dir", NULL};
    char* rm_tree[]              = {"rm", "-r", "/tmp/coreutils_tree_copy", NULL};
    char* rm_tree_bad[]          = {"rm", "-r", "/tmp/coreutils_tree_copy", NULL};
    char* chmod_octal[]          = {"chmod", "600", "/tmp/coreutils_mode", NULL};
    char* chmod_symbolic[]       = {"chmod", "u+x,g-w,o=rx", "/tmp/coreutils_mode", NULL};
    char* chmod_recursive[]      = {"chmod", "-R", "640", "/tmp/coreutils_tree", NULL};
    char* chmod_bad[]            = {"chmod", "not-a-mode", "/tmp/coreutils_mode", NULL};
    char* basename_path[]        = {"basename", "/usr/local/bin/astia", NULL};
    char* basename_suffix[]      = {"basename", "/usr/local/bin/astia.c", ".c", NULL};
    char* basename_root[]        = {"basename", "///", NULL};
    char* basename_same_suffix[] = {"basename", "name", "name", NULL};
    char* dirname_path[]         = {"dirname", "/usr/local/bin/astia", NULL};
    char* dirname_plain[]        = {"dirname", "astia", NULL};
    char* dirname_trailing[]     = {"dirname", "/usr/local/bin///", NULL};
    char* dirname_root[]         = {"dirname", "///", NULL};
    char* find_tree[]            = {"find", "/tmp/coreutils_tree", NULL};
    char* ls_tree[]              = {"ls", "/tmp/coreutils_tree", NULL};
    char* ls_col_dir[]           = {"ls", "/tmp/coreutils_ls_col", NULL};
    char* ls_color_dir[]         = {"ls", "/tmp/coreutils_ls_color", NULL};
    char* ls_wide_dir[]          = {"ls", "/tmp/coreutils_ls_wide", NULL};
    char* ls_empty_dir[]         = {"ls", "/tmp/coreutils_ls_empty", NULL};
    char* ls_plain_env[]         = {"PATH=/bin", NULL};
    char* ls_no_color_env[]      = {"PATH=/bin", "NO_COLOR=1", NULL};
    char* grep_match[]           = {"grep", "beta", NULL};
    char* grep_numbered[]        = {"grep", "-n", "beta", NULL};
    char* grep_insensitive[]     = {"grep", "-i", "BETA", NULL};
    char* grep_empty_insensitive[] = {"grep", "-i", "", NULL};
    char* grep_inverted[]          = {"grep", "-v", "beta", NULL};
    char* grep_none[]              = {"grep", "missing", NULL};
    char* grep_missing[]           = {"grep", "pattern", "/tmp/coreutils_missing", NULL};
    char* grep_dot[]               = {"grep", "a.c", NULL};
    char* grep_escaped_dot[]       = {"grep", "a\\.c", NULL};
    char* grep_star[]              = {"grep", "ab*c", NULL};
    char* grep_anchor_start[]      = {"grep", "^root", NULL};
    char* grep_anchor_end[]        = {"grep", "root$", NULL};
    char* grep_bracket[]           = {"grep", "[0-9]", NULL};
    char* grep_interval[]          = {"grep", "a\\{2\\}", NULL};
    char* grep_backref[]           = {"grep", "\\(ab\\)\\1", NULL};
    char* grep_empty_line[]        = {"grep", "^$", NULL};
    char* grep_invalid[]           = {"grep", "a\\", NULL};
    char* grep_insensitive_dot[]   = {"grep", "-i", "A.C", NULL};
    char* grep_help[]              = {"grep", "--help", NULL};
    char* sort_stdin[]             = {"sort", NULL};
    char* cut_fields[]             = {"cut", "-d", ":", "-f", "2", NULL};
    char* cut_open_end[]           = {"cut", "-d", ":", "-f", "2-", NULL};
    char* cut_open_start[]         = {"cut", "-d", ":", "-f", "-3", NULL};
    char* cut_closed_range[]       = {"cut", "-d", ":", "-f", "2-4", NULL};
    char* cut_combined_list[]      = {"cut", "-d", ":", "-f", "1,3-", NULL};
    char* cut_reversed_range[]     = {"cut", "-d", ":", "-f", "4-2", NULL};
    char* cut_dash_only[]          = {"cut", "-d", ":", "-f", "-", NULL};
    char* cut_trailing_comma[]     = {"cut", "-d", ":", "-f", "2,", NULL};
    char* tee_file[]               = {"tee", "/tmp/coreutils_tee", NULL};
    char* whoami[]                 = {"whoami", NULL};
    char* login[]                  = {"login", NULL};

    char* wc_lines_one[]   = {"wc", "-l", "/tmp/coreutils_a", NULL};
    char* wc_lines_files[] = {"wc", "-l", "/tmp/coreutils_a", "/tmp/coreutils_b", NULL};
    char* wc_lines_stdin[] = {"wc", "-l", NULL};
    char* wc_words_file[]  = {"wc", "-w", "/tmp/coreutils_a", NULL};
    char* wc_lines_bytes[] = {"wc", "-lc", "/tmp/coreutils_a", "/tmp/coreutils_b", NULL};

    /* Keep reruns deterministic when a previous QEMU run stopped midway. */
    (void) unlink("/tmp/coreutils_copy");
    (void) unlink("/tmp/coreutils_mode_copy");
    (void) unlink("/tmp/coreutils_mode_src");
    (void) unlink("/tmp/coreutils_cp_hard");
    (void) unlink("/tmp/coreutils_from_stdin");
    (void) unlink("/tmp/coreutils_moved");
    (void) unlink("/tmp/coreutils_tee");
    (void) unlink("/tmp/coreutils_touch");
    (void) unlink("/tmp/coreutils_touch_missing");
    (void) unlink("/tmp/coreutils_link");
    (void) unlink("/tmp/coreutils_hard");
    (void) unlink("/tmp/coreutils_symlink_long");
    (void) unlink("/tmp/coreutils_symlink_missing");
    (void) unlink("/tmp/coreutils_dangling");
    (void) rmdir("/tmp/coreutils_parent/child");
    (void) rmdir("/tmp/coreutils_parent");
    (void) rmdir("/tmp/coreutils_dir");
    (void) unlink("/tmp/coreutils_tree_copy/nested/file");
    (void) rmdir("/tmp/coreutils_tree_copy/nested");
    (void) rmdir("/tmp/coreutils_tree_copy");
    (void) unlink("/tmp/coreutils_tree/nested/file");
    (void) rmdir("/tmp/coreutils_tree/nested");
    (void) rmdir("/tmp/coreutils_tree/nested_copy");
    (void) rmdir("/tmp/coreutils_tree/coreutils_tree");
    (void) rmdir("/tmp/coreutils_tree");
    (void) rmdir("/tmp/coreutils_tree_bad");
    /* The minimal initrd need not ship a /tmp directory. */
    (void) mkdir("/tmp", 0777);

    if (put_file("/tmp/coreutils_a", "alpha\n") != 0 ||
        put_file("/tmp/coreutils_b", "beta\ngamma\n") != 0 ||
        put_file("/tmp/coreutils_lines", "one\ntwo\nthree\n") != 0 ||
        put_file("/tmp/coreutils_mode", "mode\n") != 0 || 0)
        return fail("fixture setup");

    if (put_file("/tmp/coreutils_mode_src", "mode\n") != 0 ||
        chmod("/tmp/coreutils_mode_src", 0640) != 0)
        return fail("cp mode fixture setup");

    struct stat dangling_stat;

    if (mkdir("/tmp/coreutils_tree", 0777) != 0 ||
        mkdir("/tmp/coreutils_tree/nested", 0777) != 0 ||
        put_file("/tmp/coreutils_tree/nested/file", "tree\n") != 0 ||
        chmod("/tmp/coreutils_tree/nested/file", 0640) != 0)
        return fail("recursive fixture setup");

    if (mkdir("/tmp/coreutils_ls_col", 0777) != 0 ||
        put_file("/tmp/coreutils_ls_col/gamma", "x\n") != 0 ||
        put_file("/tmp/coreutils_ls_col/alpha", "x\n") != 0 ||
        put_file("/tmp/coreutils_ls_col/delta", "x\n") != 0 ||
        put_file("/tmp/coreutils_ls_col/beta", "x\n") != 0 ||
        mkdir("/tmp/coreutils_ls_color", 0777) != 0 ||
        mkdir("/tmp/coreutils_ls_color/sub", 0777) != 0 ||
        mkdir("/tmp/coreutils_ls_wide", 0777) != 0 ||
        put_file("/tmp/coreutils_ls_wide/abcdefghijklmnopqrstuvwxyz0123", "x\n") != 0 ||
        mkdir("/tmp/coreutils_ls_empty", 0777) != 0)
        return fail("ls fixture setup");

    if (check("cat stdin", cat_stdin, "plain\n", 0, "plain\n", 1) ||
        check("cat stdin dash", cat_stdin_dash, "dashed\n", 0, "dashed\n", 1) ||
        check("cat file and stdin", cat_file_stdin, "tail\n", 0, "alpha\ntail\n", 1) ||
        check("cat files", cat_files, NULL, 0, "alpha\nbeta\ngamma\n", 1) ||
        check("cat missing", cat_missing, NULL, 1, NULL, 0) ||
        check("head -n", head_lines, NULL, 0, "one\ntwo\n", 1) ||
        check("head invalid", head_bad, NULL, 2, NULL, 0) ||
        check("head invalid number", head_bad_text, NULL, 2, NULL, 0) ||
        check("wc default files", wc_files, NULL, 0,
              "1 1 6 /tmp/coreutils_a\n2 2 11 /tmp/coreutils_b\n3 3 17 total\n", 1) ||
        check("wc -l file", wc_lines_one, NULL, 0, "1 /tmp/coreutils_a\n", 1) ||
        check("wc -l files", wc_lines_files, NULL, 0,
              "1 /tmp/coreutils_a\n2 /tmp/coreutils_b\n3 total\n", 1) ||
        check("wc -l stdin", wc_lines_stdin, "alpha\nbeta\n", 0, "2\n", 1) ||
        check("wc -w file", wc_words_file, NULL, 0, "1 /tmp/coreutils_a\n", 1) ||
        check("wc -lc files", wc_lines_bytes, NULL, 0,
              "1 6 /tmp/coreutils_a\n2 11 /tmp/coreutils_b\n3 17 total\n", 1) ||
        check("wc invalid", wc_bad, NULL, 1, NULL, 0) ||
        check("ls directory", ls_dir, NULL, 0, "cat\n", 0) ||
        check("ls multiple", ls_multi, NULL, 0, "/bin:\n", 0) ||
        check("ls two directories", ls_two_dirs, NULL, 0,
              "/tmp/coreutils_ls_col:\nalpha\nbeta\ndelta\ngamma\n\n"
              "/tmp/coreutils_ls_color:\nsub\n",
              1) ||
        check("ls file and directory", ls_file_dir, NULL, 0,
              "coreutils_a\n\n/tmp/coreutils_ls_empty:\n", 1) ||
        check("ls invalid", ls_bad, NULL, 1, NULL, 0) || ln_checks() != 0 ||
        ls_symlink_checks() != 0 || ls_hidden_checks() != 0 ||
        symlink("missing", "/tmp/coreutils_dangling") != 0 ||
        lstat("/tmp/coreutils_dangling", &dangling_stat) != 0 ||
        !S_ISLNK(dangling_stat.st_mode) ||
        stat("/tmp/coreutils_dangling", &dangling_stat) == 0 ||
        check("pwd", pwd, NULL, 0, "/bin\n", 1) ||
        check("whoami", whoami, NULL, 0, "root\n", 1) ||
        check("login without controlling tty", login, "missing\n", 1, NULL, 0) ||
        login_pty_checks() != 0 || check("pwd invalid", pwd_bad, NULL, 2, NULL, 0) ||
        check("echo", echo, NULL, 0, "hello Astia\n", 1) ||
        check("echo -n", echo_n, NULL, 0, "hello", 1) ||
        check("env", env, NULL, 0, "COREUTILS_TEST=present\n", 0) ||
        check_env("env sorted", env_sorted, env_known, 0, "A=2\nM=3\nZ=1\n", 1) ||
        check_env("env sorted long", env_sorted_long, env_known, 0, "A=2\nM=3\nZ=1\n",
                  1) ||
        check_env("env assignment order", env_assign, env_empty, 0, "B=2\nA=3\n", 1) ||
        check_env("env sorted assignments", env_assign_sorted, env_empty, 0, "A=3\nB=2\n",
                  1) ||
        check_env("env sorted duplicates", env_sorted, env_duplicates, 0,
                  "A=1\nB=2\nB=3\n", 1) ||
        check("env invalid name", env_invalid_name, NULL, 2, NULL, 0) ||
        check("env command", env_exec, NULL, 0, NULL, 0) ||
        check("env invalid", env_bad, NULL, 2, NULL, 0) ||
        check("uname", uname, NULL, 0, "Astia\n", 1) ||
        check("uname -a", uname_all, NULL, 0, uname_all_expected(), 1) ||
        check("uname -a -m", uname_all_machine, NULL, 0, uname_all_expected(), 1) ||
        check("uname -m -a", uname_machine_all, NULL, 0, uname_all_expected(), 1) ||
        check("uname -m -s", uname_machine_sysname, NULL, 0, "Astia x86_64\n", 1) ||
        check("uname invalid", uname_bad, NULL, 1, NULL, 0) ||
        check("true", true_args, NULL, 0, NULL, 0) ||
        check("false", false_args, NULL, 1, NULL, 0) ||
        check("poweroff invalid", poweroff_bad, NULL, 2, NULL, 0) ||
        check("reboot invalid", reboot_bad, NULL, 2, NULL, 0) ||
        check("mkdir", mkdir_one, NULL, 0, NULL, 0) ||
        !is_directory("/tmp/coreutils_dir") ||
        check("mkdir -p", mkdir_parents, NULL, 0, NULL, 0) ||
        !is_directory("/tmp/coreutils_parent/child") ||
        check("mkdir -p existing", mkdir_existing, NULL, 0, NULL, 0) ||
        check("mkdir missing parent", mkdir_bad, NULL, 1, NULL, 0) ||
        check("mkdir file parent", mkdir_file_parent, NULL, 1, NULL, 0) ||
        check("touch create", touch_create, NULL, 0, NULL, 0) ||
        !is_file("/tmp/coreutils_touch") ||
        check("touch -c missing", touch_no_create, NULL, 0, NULL, 0) ||
        is_file("/tmp/coreutils_touch_missing") ||
        check("touch selective unsupported", touch_access_only, NULL, 1, NULL, 0) ||
        check("touch modify", touch_modify, NULL, 0, NULL, 0) ||
        check("touch access and modify unsupported", touch_both, NULL, 1, NULL, 0) ||
        link("/tmp/coreutils_a", "/tmp/coreutils_cp_hard") != 0 ||
        check("cp same file", cp_self, NULL, 1, NULL, 0) ||
        !file_equals("/tmp/coreutils_a", "alpha\n") ||
        check("cp hard link", cp_hardlink, NULL, 1, NULL, 0) ||
        !file_equals("/tmp/coreutils_cp_hard", "alpha\n") ||
        unlink("/tmp/coreutils_cp_hard") != 0 ||
        check("cp symlink alias", cp_symlink, NULL, 1, NULL, 0) ||
        !file_equals("/tmp/coreutils_a", "alpha\n") ||
        check("cp -r same directory", cp_recursive_self, NULL, 1, NULL, 0) ||
        check("cp -r inside directory", cp_recursive_inside, NULL, 1, NULL, 0) ||
        is_directory("/tmp/coreutils_tree/coreutils_tree") ||
        is_directory("/tmp/coreutils_tree/nested_copy") ||
        check("cp", cp_file, NULL, 0, NULL, 0) || !is_file("/tmp/coreutils_copy") ||
        check("cp mode", cp_mode, NULL, 0, NULL, 0) ||
        (!mode_is("/tmp/coreutils_mode_copy", 0640) && fail("cp preserves file mode")) ||
        check("cp directory without -r", cp_directory_bad, NULL, 1, NULL, 0) ||
        check("ls recursive fixture", ls_tree, NULL, 0, "nested\n", 0) ||
        check("ls sorted pipe", ls_col_dir, NULL, 0, "alpha\nbeta\ndelta\ngamma\n", 1) ||
        check_pty("ls tty columns", ls_col_dir, ls_plain_env, 16,
                  "alpha  delta\nbeta   gamma\n") ||
        check_pty("ls tty zero width fallback", ls_col_dir, ls_plain_env, 0,
                  "alpha\nbeta\ndelta\ngamma\n") ||
        check_pty("ls tty long name", ls_wide_dir, ls_plain_env, 20,
                  "abcdefghijklmnopqrstuvwxyz0123\n") ||
        check_pty("ls tty color", ls_color_dir, ls_plain_env, 80,
                  "\033[34msub\033[0m\n") ||
        check_pty("ls tty no_color", ls_color_dir, ls_no_color_env, 80, "sub\n") ||
        check_env("ls pipe no color", ls_color_dir, ls_plain_env, 0, "sub\n", 1) ||
        check_output_excludes("ls empty directory", ls_empty_dir, 0, "",
                              "/tmp/coreutils_ls_empty:") ||
        check("cp -r directory", cp_directory, NULL, 0, NULL, 0) ||
        (!is_directory("/tmp/coreutils_tree_copy/nested") &&
         fail("cp -r nested directory")) ||
        (!file_equals("/tmp/coreutils_tree_copy/nested/file", "tree\n") &&
         fail("cp -r nested file")) ||
        (!mode_is("/tmp/coreutils_tree_copy/nested/file", 0640) &&
         fail("cp -r preserves file mode")) ||
        check("find directory", find_tree, NULL, 0, "/tmp/coreutils_tree/nested/file\n",
              0) ||
        check("cp stdin", cp_stdin, "from stdin\n", 0, NULL, 0) ||
        !is_file("/tmp/coreutils_from_stdin") ||
        check("cp stdin same file", cp_stdin_self, "alpha\n", 1, NULL, 0) ||
        !file_equals("/tmp/coreutils_stdin", "alpha\n") ||
        check("mv", mv_file, NULL, 0, NULL, 0) || !is_file("/tmp/coreutils_moved") ||
        check("mv replacement", mv_replace, NULL, 0, NULL, 0) ||
        is_file("/tmp/coreutils_a") || !is_file("/tmp/coreutils_moved") ||
        check("mv missing", mv_missing, NULL, 1, NULL, 0) ||
        check("rm", rm_file, NULL, 0, NULL, 0) || is_file("/tmp/coreutils_moved") ||
        check("rm missing", rm_missing, NULL, 1, NULL, 0) ||
        check("rm -f", rm_force, NULL, 0, NULL, 0) ||
        check("rm -f no operands", rm_force_empty, NULL, 0, NULL, 0) ||
        check("rm dangling symlink", rm_dangling, NULL, 0, NULL, 0) ||
        check("rm recursive", rm_recursive, NULL, 0, NULL, 0) ||
        check("chmod octal", chmod_octal, NULL, 0, NULL, 0) ||
        !has_mode("/tmp/coreutils_mode", 0600) ||
        check("chmod symbolic", chmod_symbolic, NULL, 0, NULL, 0) ||
        !has_mode("/tmp/coreutils_mode", 0705) ||
        check("chmod invalid", chmod_bad, NULL, 1, NULL, 0) ||
        check("chmod recursive", chmod_recursive, NULL, 0, NULL, 0) ||
        !has_mode("/tmp/coreutils_tree", 0640) ||
        !has_mode("/tmp/coreutils_tree/nested/file", 0640) ||
        check("rm -r tree", rm_tree, NULL, 0, NULL, 0) ||
        check("rm -r missing", rm_tree_bad, NULL, 1, NULL, 0) ||
        check("basename", basename_path, NULL, 0, "astia\n", 1) ||
        check("basename suffix", basename_suffix, NULL, 0, "astia\n", 1) ||
        check("basename root", basename_root, NULL, 0, "/\n", 1) ||
        check("basename identical suffix", basename_same_suffix, NULL, 0, "name\n", 1) ||
        check("dirname", dirname_path, NULL, 0, "/usr/local/bin\n", 1) ||
        check("dirname plain", dirname_plain, NULL, 0, ".\n", 1) ||
        check("dirname trailing", dirname_trailing, NULL, 0, "/usr/local\n", 1) ||
        check("dirname root", dirname_root, NULL, 0, "/\n", 1) ||
        check("grep match", grep_match, "alpha\nbeta\ngamma\n", 0, NULL, 0) ||
        check("grep -n", grep_numbered, "alpha\nbeta\n", 0, "2:beta\n", 1) ||
        check("grep -i", grep_insensitive, "alpha\nBeta\ngamma\n", 0, NULL, 0) ||
        check("grep -i empty", grep_empty_insensitive, "\n", 0, "\n", 1) ||
        check("grep -v", grep_inverted, "alpha\nbeta\n", 0, NULL, 0) ||
        check("grep no match", grep_none, "alpha\nbeta\n", 1, NULL, 0) ||
        check("grep missing file", grep_missing, NULL, 2, NULL, 0) ||
        check("grep dot", grep_dot, "abc\na.c\nxyz\n", 0, "abc\na.c\n", 1) ||
        check("grep escaped dot", grep_escaped_dot, "abc\na.c\nxyz\n", 0, "a.c\n", 1) ||
        check("grep star", grep_star, "ac\nabc\nabbbc\nadc\n", 0, "ac\nabc\nabbbc\n",
              1) ||
        check("grep anchor start", grep_anchor_start, "root:x\nxroot\n", 0, "root:x\n",
              1) ||
        check("grep anchor end", grep_anchor_end, "xroot\nrootx\n", 0, "xroot\n", 1) ||
        check("grep bracket", grep_bracket, "abc\na1b\n", 0, "a1b\n", 1) ||
        check("grep interval", grep_interval, "a\naa\naaa\n", 0, "aa\naaa\n", 1) ||
        check("grep backref", grep_backref, "ab\nabab\n", 0, "abab\n", 1) ||
        check("grep empty line", grep_empty_line, "a\n\nb\n", 0, "\n", 1) ||
        check("grep invalid regex", grep_invalid, NULL, 2, NULL, 0) ||
        check("grep -i dot", grep_insensitive_dot, "abc\n", 0, "abc\n", 1) ||
        check("grep help", grep_help, NULL, 0,
              "Usage: grep [-i] [-n] [-v] PATTERN [FILE]...\n\n"
              "Print lines matching the POSIX basic regular expression PATTERN; - means "
              "standard input.\n"
              "Supported: ^ $ . * [ ] \\ \\( \\) \\{m,n\\} and \\1-\\9.\n",
              1) ||
        check("sort stdin", sort_stdin, "zulu\nalpha\n", 0, NULL, 0) ||
        check("cut fields", cut_fields, "one:two:three\n", 0, NULL, 0) ||
        check("cut open-ended range", cut_open_end, "one:two:three\n", 0, "two:three\n",
              1) ||
        check("cut leading open range", cut_open_start, "one:two:three\n", 0,
              "one:two:three\n", 1) ||
        check("cut closed range", cut_closed_range, "one:two:three\n", 0, "two:three\n",
              1) ||
        check("cut combined list", cut_combined_list, "one:two:three\n", 0, "one:three\n",
              1) ||
        check("cut reversed range", cut_reversed_range, "one:two:three\n", 2, NULL, 0) ||
        check("cut dash-only range", cut_dash_only, "one:two:three\n", 2, NULL, 0) ||
        check("cut trailing comma", cut_trailing_comma, "one:two:three\n", 2, NULL, 0) ||
        check("tee", tee_file, "tee input\n", 0, NULL, 0) ||
        !file_equals("/tmp/coreutils_tee", "tee input\n") || 0)
        return 1;

    if (sort_empty_checks() != 0)
        return 1;

    if (long_line_checks() != 0)
        return 1;

    if (head_header_checks() != 0)
        return 1;

    if (sort_growth_checks() != 0)
        return 1;

    if (mv_directory_checks() != 0)
        return 1;

    if (mv_cross_device_mode_checks() != 0)
        return 1;

    if (mv_cross_device_symlink_checks() != 0)
        return 1;

    if (rm_directory_checks() != 0)
        return 1;

    (void) unlink("/tmp/coreutils_mode_src");
    (void) unlink("/tmp/coreutils_mode_copy");

    if (find_symlink_checks() != 0)
        return 1;

    if (tree_walk_checks() != 0)
        return 1;

    const char* help[] = {"cat",    "head",  "wc",    "ls",       "find",    "pwd",
                          "uname",  "echo",  "env",   "true",     "false",   "mkdir",
                          "rm",     "cp",    "mv",    "grep",     "sort",    "cut",
                          "tee",    "touch", "chmod", "basename", "dirname", "poweroff",
                          "reboot", NULL};

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
