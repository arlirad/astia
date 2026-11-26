#include "internal/syslog_sink.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

enum {
    syslog_ident_max           = 64,
    syslog_atfork_unregistered = 0,
    syslog_atfork_registering  = 1,
    syslog_atfork_registered   = 2,
};

static char syslog_ident[syslog_ident_max] = "syslog";
static int  syslog_facility                = LOG_USER;
static int  syslog_options;
static int  syslog_fd = -1;
static int  syslog_unavailable;
static int  syslog_mask = LOG_UPTO(LOG_DEBUG);
static int  syslog_lock;
static int  syslog_atfork_state;

static void syslog_lock_acquire(void) {
    while (__atomic_exchange_n(&syslog_lock, 1, __ATOMIC_ACQUIRE) != 0) {
    }
}

static void syslog_lock_release(void) {
    __atomic_store_n(&syslog_lock, 0, __ATOMIC_RELEASE);
}

static void syslog_atfork_child(void) {
    __atomic_store_n(&syslog_lock, 0, __ATOMIC_RELEASE);
}

static void syslog_prepare_fork_safety(void) {
    int expected = syslog_atfork_unregistered;

    if (__atomic_load_n(&syslog_atfork_state, __ATOMIC_ACQUIRE) ==
        syslog_atfork_registered)
        return;

    if (!__atomic_compare_exchange_n(&syslog_atfork_state, &expected,
                                     syslog_atfork_registering, 0, __ATOMIC_ACQ_REL,
                                     __ATOMIC_ACQUIRE))
        return;

    if (pthread_atfork(NULL, NULL, syslog_atfork_child) != 0)
        __atomic_store_n(&syslog_atfork_state, syslog_atfork_unregistered,
                         __ATOMIC_RELEASE);
    else
        __atomic_store_n(&syslog_atfork_state, syslog_atfork_registered,
                         __ATOMIC_RELEASE);
}

static void syslog_report_failure(int error) {
    char diagnostic[160];

    const char* reason = strerror(error);

    int length;

    length = snprintf(diagnostic, sizeof diagnostic, "syslog: cannot open log sink: %s\n",
                      reason);
    if (length > 0)
        (void) write(STDERR_FILENO, diagnostic,
                     (size_t) length < sizeof diagnostic ? (size_t) length
                                                         : sizeof diagnostic - 1);
}

static int syslog_write_all(int fd, const char* record, size_t length) {
    while (length != 0) {
        long written = write(fd, record, length);
        if (written < 0) {
            if (errno == EINTR)
                continue;

            return -1;
        }

        if (written == 0)
            return -1;

        record += (size_t) written;
        length -= (size_t) written;
    }

    return 0;
}

static int syslog_open_sink(void) {
    int fd;

    if (syslog_fd >= 0)
        return 0;

    if (syslog_unavailable)
        return -1;

    fd = open(__astia_syslog_sink_path(), O_WRONLY | O_APPEND | O_CLOEXEC);
    if (fd < 0) {
        int error = errno;

        syslog_unavailable = 1;

        syslog_report_failure(error);

        return -1;
    }

    syslog_fd = fd;

    return 0;
}

static void syslog_fail_sink(void) {
    int error = errno;

    if (syslog_fd >= 0) {
        (void) close(syslog_fd);

        syslog_fd = -1;
    }

    if (!syslog_unavailable) {
        syslog_unavailable = 1;

        syslog_report_failure(error);
    }
}

static size_t syslog_append_result(int written, size_t used, size_t capacity) {
    if (written < 0)
        return used;

    return (size_t) written < capacity - used ? used + (size_t) written : capacity - 1;
}

static size_t syslog_format_record(char* record, int priority, const char* format,
                                   va_list ap) {
    const size_t capacity = ASTIA_SYSLOG_RECORD_MAX;
    int          facility = priority & LOG_FACMASK;
    size_t       used;
    int          written;

    if (facility == 0)
        facility = syslog_facility;

    priority = facility | LOG_PRI(priority);

    written = snprintf(record, capacity, "<%d>%s", priority, syslog_ident);
    used    = syslog_append_result(written, 0, capacity);

    if (syslog_options & LOG_PID) {
        written = snprintf(record + used, capacity - used, "[%d]", getpid());
        used    = syslog_append_result(written, used, capacity);
    }

    written = snprintf(record + used, capacity - used, ": ");
    used    = syslog_append_result(written, used, capacity);

    written = vsnprintf(record + used, capacity - used, format, ap);
    if (written < 0) {
        record[used] = '\n';

        return used + 1;
    }

    if ((size_t) written < capacity - used) {
        record[used + (size_t) written] = '\n';

        return used + (size_t) written + 1;
    }

    if (capacity >= 4)
        memcpy(record + capacity - 4, "...\n", 4);

    return capacity;
}

void openlog(const char* ident, int option, int facility) {
    size_t length;

    syslog_prepare_fork_safety();
    syslog_lock_acquire();

    syslog_options  = option;
    syslog_facility = facility & LOG_FACMASK;

    if (ident == NULL)
        ident = "syslog";

    length = strnlen(ident, syslog_ident_max - 1);

    memcpy(syslog_ident, ident, length);

    syslog_ident[length] = '\0';

    if (option & LOG_NDELAY)
        (void) syslog_open_sink();

    syslog_lock_release();
}

void closelog(void) {
    syslog_prepare_fork_safety();
    syslog_lock_acquire();

    if (syslog_fd >= 0) {
        (void) close(syslog_fd);

        syslog_fd = -1;
    }

    syslog_unavailable = 0;
    syslog_options     = 0;
    syslog_facility    = LOG_USER;

    strcpy(syslog_ident, "syslog");

    syslog_lock_release();
}

int setlogmask(int mask) {
    int previous = __atomic_load_n(&syslog_mask, __ATOMIC_ACQUIRE);

    if (mask != 0)
        __atomic_store_n(&syslog_mask, mask, __ATOMIC_RELEASE);

    return previous;
}

void vsyslog(int priority, const char* format, va_list ap) {
    char   record[ASTIA_SYSLOG_RECORD_MAX];
    size_t length;
    int    saved_errno = errno;

    if (format == NULL || (priority & ~(LOG_PRIMASK | LOG_FACMASK)) != 0) {
        errno = EINVAL;

        return;
    }

    if ((__atomic_load_n(&syslog_mask, __ATOMIC_ACQUIRE) & LOG_MASK(LOG_PRI(priority))) ==
        0) {
        errno = saved_errno;

        return;
    }

    syslog_prepare_fork_safety();
    syslog_lock_acquire();

    length = syslog_format_record(record, priority, format, ap);

    if (syslog_open_sink() == 0) {
        if (syslog_write_all(syslog_fd, record, length) != 0)
            syslog_fail_sink();
    }

    if (syslog_options & LOG_PERROR)
        (void) syslog_write_all(STDERR_FILENO, record, length);

    syslog_lock_release();

    errno = saved_errno;
}

void syslog(int priority, const char* format, ...) {
    va_list ap;

    va_start(ap, format);

    vsyslog(priority, format, ap);

    va_end(ap);
}
