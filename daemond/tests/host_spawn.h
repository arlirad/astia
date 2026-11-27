int spawnve(const char* path, char* const argv[], char* const envp[]);

/* Astia assigns the service process group without the host's EACCES-after-
 * exec rule.  The harness port accepts the already-correct group as success. */
int host_setpgid(int pid, int process_group);
