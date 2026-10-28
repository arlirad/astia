#pragma once

/* The on-wire protocol is deliberately tiny: one ASCII request terminated by
 * LF, followed by one bounded ASCII response terminated by LF. */
#define DAEMON_SOCKET_PATH "/run/daemon.sock"
#define DAEMON_SOCKET_FALLBACK "/tmp/daemon.sock"
#define DAEMON_PROTOCOL_MAX 128
#define DAEMON_RESPONSE_MAX 512
