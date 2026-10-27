#include "common.h"
#include <pwd.h>

int main(int argc, char** argv) {
    struct passwd* account;
    if (argc > 1) {
        if (cu_is_help(argv[1])) return cu_help("whoami", "", "Print the effective account name.");
        if (cu_is_version(argv[1])) return cu_version("whoami");
        return cu_usage("whoami", "");
    }
    account = getpwuid((uid_t) geteuid());
    if (account == NULL) return cu_error("whoami", "cannot resolve effective account");
    return cu_puts(account->pw_name) < 0 || cu_puts("\n") < 0;
}
