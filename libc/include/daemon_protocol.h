#pragma once

/* The on-wire protocol is deliberately tiny: one ASCII request terminated by
 * LF, followed by one bounded ASCII response terminated by LF. */
#ifndef DAEMON_SOCKET_PATH
#define DAEMON_SOCKET_PATH "/run/daemon.sock"
#endif
#ifndef DAEMON_SOCKET_FALLBACK
#define DAEMON_SOCKET_FALLBACK "/tmp/daemon.sock"
#endif
#define DAEMON_PROTOCOL_MAX 128
#define DAEMON_RESPONSE_MAX 512
