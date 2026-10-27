#include "common.h"
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* There is no password backend yet. Only `x` (or an empty, explicitly
 * passwordless field) is supported; shadow, crypt, and PAM are not used. */
static int read_name(char* name, unsigned long size) {
    long length = read(0, name, size - 1);
    if (length <= 0) return -1;
    name[length] = '\0';
    while (length > 0 && (name[length - 1] == '\n' || name[length - 1] == '\r'))
        name[--length] = '\0';
    return length == 0 || strchr(name, ':') != NULL ? -1 : 0;
}

static int collect_groups(const char* user, gid_t primary, gid_t groups[], int* count) {
    struct group* group;
    *count = 0;
    setgrent();
    while ((group = getgrent()) != NULL) {
        if (group->gr_gid == primary) continue;
        for (char** member = group->gr_mem; *member != NULL; ++member) {
            if (strcmp(*member, user) == 0) {
                if (*count == 16) { endgrent(); return -1; }
                groups[(*count)++] = group->gr_gid;
                break;
            }
        }
    }
    endgrent();
    return 0;
}

int main(int argc, char** argv) {
    char name[256], home_env[320], shell_env[320], user_env[320];
    char* shell_argv[2]; char* envp[5]; gid_t groups[16]; int group_count;
    struct passwd* account;
    if (argc != 1) return cu_usage("login", "");
    if (!isatty(0)) return cu_error("login", "controlling terminal required");
    cu_puts_fd(CU_STDOUT, "login: ");
    if (read_name(name, sizeof name) != 0) return cu_error("login", "invalid username");
    account = getpwnam(name);
    if (account == NULL) return cu_error("login", "unknown local account");
    if (strcmp(account->pw_passwd, "x") != 0 && account->pw_passwd[0] != '\0')
        return cu_error("login", "password authentication is unsupported (use x or empty password field)");
    if (collect_groups(account->pw_name, account->pw_gid, groups, &group_count) != 0)
        return cu_error("login", "too many supplementary groups");
    /* setgroups is privileged, so it precedes setgid/setuid. */
    if (setgroups(group_count, groups) != 0 || setgid(account->pw_gid) != 0 ||
        setuid(account->pw_uid) != 0)
        return cu_error("login", "cannot establish account credentials");
    (void) setsid();
    (void) tcsetpgrp(0, getpgrp());
    if (chdir(account->pw_dir) != 0) return cu_error("login", "cannot enter home directory");
    snprintf(home_env, sizeof home_env, "HOME=%s", account->pw_dir);
    snprintf(shell_env, sizeof shell_env, "SHELL=%s", account->pw_shell);
    snprintf(user_env, sizeof user_env, "USER=%s", account->pw_name);
    envp[0] = home_env; envp[1] = shell_env; envp[2] = user_env; envp[3] = "PATH=/bin"; envp[4] = NULL;
    shell_argv[0] = account->pw_shell; shell_argv[1] = NULL;
    execve(account->pw_shell, shell_argv, envp);
    return cu_error("login", "cannot exec user shell");
}
