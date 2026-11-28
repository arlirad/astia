#include "../../kernel/include/astia/api/syscalls.h"
#include "common.h"
#include "ps_state.h"

#include <stdio.h>

int main(int argc, char** argv) {
    astia_process_info entries[ASTIA_PROCESS_SNAPSHOT_MAX];

    char line[160];
    long count;

    (void) argv;

    if (argc != 1)
        return cu_usage("ps", "");

    count = astia_process_snapshot(entries, ASTIA_PROCESS_SNAPSHOT_MAX);
    if (count < 0)
        return cu_error("ps", "process snapshot unavailable");

    if (cu_puts("PID    PPID   PGID   UID    GID    STAT CTTY       COMMAND\n") < 0)
        return 1;

    for (long i = 0; i < count; ++i) {
        const char* ctty = entries[i].has_ctty ? entries[i].ctty : "-";

        int n = snprintf(line, sizeof line, "%-6d %-6d %-6d %-6u %-6u %-4c %-10s %s\n",
                         entries[i].pid, entries[i].ppid, entries[i].pgid, entries[i].uid,
                         entries[i].gid, cu_ps_state_letter(entries[i].state), ctty,
                         entries[i].name[0] ? entries[i].name : "?");

        if (n < 0 || (size_t) n >= sizeof line ||
            cu_write_all(CU_STDOUT, line, (size_t) n) < 0)
            return 1;
    }

    return 0;
}
