#pragma once

/* One supervised service process group to stop.  A negative pid marks a
 * service that is not running and therefore needs no signal. */
struct stop_process {
    int pid;
    int pgid;
    int status;
    int exited;
    int escalated;
};

/* Sends SIGTERM to every running process group, waits up to grace_ms for the
 * children to exit, sends SIGKILL to survivors, and waits up to kill_ms more.
 * Returns 0 only when every process was reaped; otherwise -1 with errno set
 * to the first signal failure or ETIMEDOUT. */
int stop_processes(struct stop_process* processes, int count, int grace_ms, int kill_ms);
