#pragma once

/* Entry point for the native service supervisor.  The daemon owns the
 * non-shell /etc/daemond YAML units, service process groups, reaping and
 * restart policy; init only starts and waits for it. */
int astia_daemon_run(const char* unit_directory);
