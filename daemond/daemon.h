#pragma once

/* Entry point for the native service supervisor.  The daemon owns the
 * non-shell /etc/rc format, service process groups, reaping and restart
 * policy; init only starts and waits for it. */
int astia_daemon_run(const char* rc_path);
