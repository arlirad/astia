#pragma once

/* Native, non-shell /etc/rc policy.  A line is:
 *
 *   /absolute/program argv0 arg1 ... [respawn[=N]]
 *
 * Tokens are separated by spaces or tabs; blank lines and lines beginning
 * with '#' are ignored.  respawn means at most three restarts; respawn=N,
 * restart=N, or max-restarts=N selects a limit (0..16).  There is no quote,
 * expansion, redirection, or pipeline syntax. */
int astia_service_manager(const char* rc_path);
