#pragma once

/*
 * astia libc — <syslog.h>
 *
 * Local daemon logging.  Records are formatted as `<PRI>ident[pid]: message`
 * and written as one bounded line to /dev/console.  The sink serializes with
 * the kernel console, so records may interleave with kernel messages but not
 * with other syslog records.  Remote syslog, journald, and structured logging
 * are not implemented.
 *
 * LOG_CONS, LOG_ODELAY, and LOG_NOWAIT are accepted no-ops.  LOG_NDELAY opens
 * the console immediately in openlog(); otherwise it is opened on the first
 * record.  LOG_PERROR additionally writes the record to stderr.  Identifiers
 * are copied and truncated to 63 bytes.  A record longer than
 * ASTIA_SYSLOG_RECORD_MAX is truncated with a trailing "..." marker instead
 * of being split across lines.  The interface is thread-safe and may be used
 * without openlog().
 */

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_EMERG 0
#define LOG_ALERT 1
#define LOG_CRIT 2
#define LOG_ERR 3
#define LOG_WARNING 4
#define LOG_NOTICE 5
#define LOG_INFO 6
#define LOG_DEBUG 7

#define LOG_PRIMASK 0x07
#define LOG_PRI(p) ((p) & LOG_PRIMASK)
#define LOG_MAKEPRI(fac, pri) ((fac) | ((pri) & LOG_PRIMASK))
#define LOG_FACMASK 0x03f8
#define LOG_FAC(p) (((p) & LOG_FACMASK) >> 3)

#define LOG_KERN (0 << 3)
#define LOG_USER (1 << 3)
#define LOG_MAIL (2 << 3)
#define LOG_DAEMON (3 << 3)
#define LOG_AUTH (4 << 3)
#define LOG_SYSLOG (5 << 3)
#define LOG_LPR (6 << 3)
#define LOG_NEWS (7 << 3)
#define LOG_UUCP (8 << 3)
#define LOG_CRON (9 << 3)
#define LOG_AUTHPRIV (10 << 3)
#define LOG_FTP (11 << 3)
#define LOG_LOCAL0 (16 << 3)
#define LOG_LOCAL1 (17 << 3)
#define LOG_LOCAL2 (18 << 3)
#define LOG_LOCAL3 (19 << 3)
#define LOG_LOCAL4 (20 << 3)
#define LOG_LOCAL5 (21 << 3)
#define LOG_LOCAL6 (22 << 3)
#define LOG_LOCAL7 (23 << 3)

#define LOG_PID 0x01
#define LOG_CONS 0x02
#define LOG_ODELAY 0x04
#define LOG_NDELAY 0x08
#define LOG_NOWAIT 0x10
#define LOG_PERROR 0x20

#define LOG_MASK(pri) (1 << (pri))
#define LOG_UPTO(pri) ((1 << ((pri) + 1)) - 1)

#define ASTIA_SYSLOG_RECORD_MAX 1024

#if defined(__GNUC__) || defined(__clang__)
#define ASTIA_SYSLOG_PRINTF(fmt_idx, first_arg) \
    __attribute__((format(printf, fmt_idx, first_arg)))
#else
#define ASTIA_SYSLOG_PRINTF(fmt_idx, first_arg)
#endif

void openlog(const char* ident, int option, int facility);
void closelog(void);
void syslog(int priority, const char* format, ...) ASTIA_SYSLOG_PRINTF(2, 3);
void vsyslog(int priority, const char* format, va_list ap) ASTIA_SYSLOG_PRINTF(2, 0);
int  setlogmask(int mask);

#ifdef __cplusplus
} /* extern "C" */
#endif
