# Astia libc process helpers

The process interfaces in `include/unistd.h` and `include/sys/wait.h` are
thin wrappers over Astia's native process ABI.  `execl`, `execle`, and
`execlp` construct the `argv` vector required by `execve`; they only return
`-1` when the native image replacement fails.  `wait` is the `waitpid(-1,
status, 0)` convenience form.  No host filesystem or process emulation is
used by the Astia build.

## Feature-test macros

Astia's freestanding libc deliberately has no glibc-style feature-test
layer: there is no `<features.h>` and no `__USE_*` derivation.  The
request macros `_POSIX_C_SOURCE`, `_XOPEN_SOURCE`, `_GNU_SOURCE`, and
`_DEFAULT_SOURCE` are accepted but inert; they never gate a declaration
or hide an implementation.  Every implemented header and API is
unconditional, and that set is the documented API superset.  Requests for
unimplemented APIs remain missing at the symbol level rather than being
faked behind a preprocessor branch.

The `libc_feature_macros` test compiles every installed header standalone
with strict C11 diagnostics in the default mode and with each request
macro individually and in combination.  It also verifies that request
macros remain visible, that no derived `__USE_*`, `__GLIBC__`,
`__header_inline`, or `__UBCLIBC__` macros appear, and that
unconditional APIs such as `strdup` and `fileno` still compile.

## Local syslog

`include/syslog.h` implements `openlog`, `closelog`, `syslog`, `vsyslog`,
and `setlogmask` for daemons that have detached from their controlling
terminal.  The only sink is `/dev/console`: records are formatted as one
bounded line, `<PRI>ident[pid]: message`, with the standard BSD/Linux
facility and priority values.  The console serializes writes with kernel
console output, so records can interleave with kernel messages but not
with other syslog records.

`openlog` copies the identifier (truncated to 63 bytes), the facility, and
the option mask.  `LOG_PID` adds `[pid]`, `LOG_PERROR` duplicates the
record to stderr, and `LOG_NDELAY` opens the console immediately instead
of on the first record.  `LOG_CONS`, `LOG_ODELAY`, and `LOG_NOWAIT` are
documented no-ops.  `setlogmask` starts at `LOG_UPTO(LOG_DEBUG)`;
`setlogmask(0)` leaves the mask unchanged.  `%m` expands to
`strerror(errno)` in the shared `snprintf` formatter.

The sink is opened lazily and closed by `closelog`; a failed open is
reported once on stderr (when available) and never aborts the process.
Writing is serialized by a spin lock, children reset that lock through
`pthread_atfork`, and `exec` naturally starts from the default state.  A
record longer than `ASTIA_SYSLOG_RECORD_MAX` (1024 bytes) is truncated
with a trailing `...` marker rather than split across lines.  The state is
per process, so a forked daemon keeps logging to the same console unless
it calls `openlog` again.
