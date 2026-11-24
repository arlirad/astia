#include <sys/stat.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#define ZSH_LSTAT_TARGET "/tmp/zsh-lstat-target"
#define ZSH_LSTAT_LINK "/tmp/zsh-lstat-link"
#define ZSH_REALPATH_DIR "/tmp/zsh-realpath-dir"
#define ZSH_REALPATH_LINK "/tmp/zsh-realpath-link"

static int run_zsh(const char* command) {
    char* argv[] = {"/bin/zsh", "-f", "-c", (char*) command, NULL};
    int   status = 0;
    int   child  = spawnve("/bin/zsh", argv, NULL);
    if (child < 0)
        return 100 + errno;
    if (waitpid(child, &status, 0) != child)
        return 110;
    return status == 0 ? 0 : 120 + WEXITSTATUS(status);
}

static void check_zsh(int* result, int code, const char* command) {
    if (*result == 0 && run_zsh(command) != 0)
        *result = code;
}

static int create_link_fixture(void) {
    int fd;
    int result;

    (void) unlink(ZSH_LSTAT_LINK);
    (void) unlink(ZSH_LSTAT_TARGET);
    fd = open(ZSH_LSTAT_TARGET, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0)
        return -1;
    result = write(fd, "lstat\n", 6) == 6 ? 0 : -1;
    if (close(fd) != 0)
        result = -1;
    if (result != 0)
        return -1;
    return symlink(ZSH_LSTAT_TARGET, ZSH_LSTAT_LINK);
}

static int remove_link_fixture(void) {
    int result = 0;

    if (unlink(ZSH_LSTAT_LINK) != 0)
        result = 1;
    if (unlink(ZSH_LSTAT_TARGET) != 0)
        result = 1;
    return result;
}

static int create_realpath_fixture(void) {
    (void) unlink(ZSH_REALPATH_LINK);
    (void) rmdir(ZSH_REALPATH_DIR);
    if (mkdir(ZSH_REALPATH_DIR, 0755) != 0)
        return -1;
    return symlink(ZSH_REALPATH_DIR, ZSH_REALPATH_LINK);
}

static int remove_realpath_fixture(void) {
    int result = 0;

    if (unlink(ZSH_REALPATH_LINK) != 0)
        result = 1;
    if (rmdir(ZSH_REALPATH_DIR) != 0)
        result = 1;
    return result;
}

int main(void) {
    int result = 0;

    if (setenv("ASTIA_INHERITED", "parent", 1) != 0 || setenv("PATH", "/bin", 1) != 0)
        return 9;
    if (run_zsh("test \"$ASTIA_INHERITED\" = parent") != 0)
        return 20;
    if (run_zsh("echo redirection >/tmp/zsh-redir") != 0)
        return 21;
    if (run_zsh("/bin/echo external | wc -l") != 0)
        return 22;
    if (run_zsh("echo builtin | wc -l") != 0)
        return 23;
    if (run_zsh(": & wait") != 0)
        return 24;
    if (run_zsh("trap 'echo astia-signal-ok' INT; kill -INT $$") != 0)
        return 25;
    if (run_zsh("zmodload zsh/datetime") != 0)
        return 26;
    if (run_zsh("zmodload zsh/datetime && "
                "[ \"$(strftime '%Y-%m-%d %H:%M:%S' 1795442709)\" = "
                "'2026-11-23 14:05:09' ]") != 0)
        return 27;
    if (run_zsh("zmodload zsh/datetime && "
                "[ \"$(strftime -r '%Y-%m-%d %H:%M:%S' "
                "'2026-11-23 14:05:09')\" = '1795442709' ]") != 0)
        return 28;
    if (run_zsh("zmodload zsh/datetime && "
                "[ \"$(strftime -r '%Y-%m-%d %H:%M:%S' "
                "'1969-12-31 23:59:59')\" = '-1' ]") != 0)
        return 29;
    if (run_zsh("zmodload zsh/datetime && "
                "strftime -q -r '%Y-%m-%d' '2026-13-01'") == 0)
        return 30;
    if (run_zsh("zmodload zsh/datetime && "
                "[ \"$(strftime -r '%Y-%m-%d' '2026-11-23')\" = '1795392000' ]") != 0)
        return 31;

    if (create_link_fixture() != 0) {
        (void) remove_link_fixture();
        return 32;
    }
    check_zsh(&result, 33,
              "[[ -L " ZSH_LSTAT_LINK " ]] && "
              "[[ ! -L " ZSH_LSTAT_TARGET " ]]");
    check_zsh(&result, 34,
              "zmodload zsh/stat && "
              "zstat -L -A st " ZSH_LSTAT_LINK " && "
              "(( (st[3] & 8#170000) == 8#120000 ))");
    if (remove_link_fixture() != 0 && result == 0)
        result = 35;

    if (create_realpath_fixture() != 0) {
        (void) remove_realpath_fixture();
        return 36;
    }
    check_zsh(&result, 37,
              "cd -L " ZSH_REALPATH_LINK " && "
              "[[ ${PWD:A} = " ZSH_REALPATH_DIR " ]]");
    check_zsh(&result, 38,
              "cd -L " ZSH_REALPATH_LINK " && "
              "[[ ${PWD:P} = " ZSH_REALPATH_DIR " ]]");
    if (remove_realpath_fixture() != 0 && result == 0)
        result = 39;
    return result;
}
