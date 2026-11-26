#include "internal/syslog_sink.h"

#include <syslog.h>

#if defined(__GNUC__)
#include_next <pthread.h>
#else
#include <pthread.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            fails++;                                               \
        }                                                          \
    } while (0)

static char sink_path[256] = "/tmp/astia_syslog_test.log";

const char* __astia_syslog_sink_path(void) {
    return sink_path;
}

static int captured_stderr = -1;
static int saved_stderr    = -1;

static long sink_read(char* output, size_t capacity) {
    int  fd;
    long total = 0;

    if (capacity != 0)
        output[0] = '\0';

    fd = open(sink_path, O_RDONLY);
    if (fd < 0)
        return -1;

    while ((size_t) total + 1 < capacity) {
        long got = read(fd, output + total, capacity - (size_t) total - 1);
        if (got <= 0)
            break;

        total += got;
    }

    output[total] = '\0';

    close(fd);

    return total;
}

static void new_sink(void) {
    char name[] = "/tmp/astia_syslog_XXXXXX";
    int  fd;

    closelog();

    fd = mkstemp(name);

    CHECK(fd >= 0);

    if (fd < 0)
        return;

    close(fd);
    strcpy(sink_path, name);
}

static void clear_sink(void) {
    int fd = open(sink_path, O_WRONLY | O_TRUNC);

    CHECK(fd >= 0);

    if (fd >= 0)
        close(fd);
}

static void expect_sink(const char* expected) {
    char output[ASTIA_SYSLOG_RECORD_MAX + 64];
    long length;

    length = sink_read(output, sizeof output);

    CHECK(length >= 0);
    CHECK(strcmp(output, expected) == 0);

    if (length < 0 || strcmp(output, expected) != 0)
        printf("expected [%s] got [%s]\n", expected, output);
}

static void expect_empty_sink(void) {
    char output[8];
    long length;

    length = sink_read(output, sizeof output);

    CHECK(length == 0);

    if (length != 0)
        printf("expected an empty sink, got [%s]\n", output);
}

static void stderr_capture_begin(void) {
    int fds[2];

    if (pipe(fds) != 0) {
        CHECK(0);

        return;
    }

    saved_stderr = dup(STDERR_FILENO);

    CHECK(saved_stderr >= 0);

    if (saved_stderr < 0) {
        close(fds[0]);
        close(fds[1]);

        return;
    }

    CHECK(dup2(fds[1], STDERR_FILENO) == STDERR_FILENO);
    close(fds[1]);

    captured_stderr = fds[0];
}

static long stderr_capture_end(char* output, size_t capacity) {
    long total = 0;

    CHECK(dup2(saved_stderr, STDERR_FILENO) == STDERR_FILENO);
    close(saved_stderr);

    saved_stderr = -1;

    while ((size_t) total + 1 < capacity) {
        long got = read(captured_stderr, output + total, capacity - (size_t) total - 1);
        if (got <= 0)
            break;

        total += got;
    }

    output[total] = '\0';

    close(captured_stderr);

    captured_stderr = -1;

    return total;
}

static int counted_occurrences(const char* text, const char* needle) {
    int count = 0;

    const char* at = text;

    while ((at = strstr(at, needle)) != NULL) {
        count++;

        at += strlen(needle);
    }

    return count;
}

static void test_default_mask(void) {
    new_sink();
    openlog("default", 0, LOG_USER);

    syslog(LOG_DEBUG, "debug");

    expect_sink("<15>default: debug\n");
}

static void test_priority_filtering(void) {
    int previous;

    new_sink();

    previous = setlogmask(LOG_UPTO(LOG_ERR));

    CHECK(previous == LOG_UPTO(LOG_DEBUG));

    openlog("filter", 0, LOG_DAEMON);

    syslog(LOG_INFO, "info");
    expect_empty_sink();

    syslog(LOG_ERR, "error");
    expect_sink("<27>filter: error\n");

    previous = setlogmask(LOG_UPTO(LOG_WARNING));

    CHECK(previous == LOG_UPTO(LOG_ERR));

    previous = setlogmask(0);

    CHECK(previous == LOG_UPTO(LOG_WARNING));

    clear_sink();

    syslog(LOG_NOTICE, "notice");
    expect_empty_sink();

    syslog(LOG_WARNING, "warning");
    expect_sink("<28>filter: warning\n");

    setlogmask(LOG_UPTO(LOG_DEBUG));
}

static void test_openlog_formatting(void) {
    char expected[160];

    new_sink();

    openlog("testd", LOG_PID, LOG_DAEMON);
    syslog(LOG_INFO, "hello %d", 42);

    snprintf(expected, sizeof expected, "<30>testd[%d]: hello 42\n", getpid());
    expect_sink(expected);

    clear_sink();
    openlog("authd", 0, LOG_AUTH);
    syslog(LOG_ERR, "failure");
    expect_sink("<35>authd: failure\n");

    clear_sink();
    syslog(LOG_USER | LOG_DEBUG, "facility");
    expect_sink("<15>authd: facility\n");

    closelog();
    clear_sink();
    syslog(LOG_INFO, "after");
    expect_sink("<14>syslog: after\n");
}

static void test_percent_m(void) {
    char expected[160];

    new_sink();
    openlog("errd", LOG_PID, LOG_DAEMON);

    errno = ENOENT;

    syslog(LOG_ERR, "open %s: %m (%d)", "file", 7);

    CHECK(errno == ENOENT);

    snprintf(expected, sizeof expected,
             "<27>errd[%d]: open file: "
             "No such file or directory (7)\n",
             getpid());
    expect_sink(expected);

    clear_sink();

    errno = 9999;

    syslog(LOG_ERR, "%m");
    snprintf(expected, sizeof expected, "<27>errd[%d]: Unknown error\n", getpid());
    expect_sink(expected);

    clear_sink();
    syslog(LOG_ERR, "literal %%m");
    snprintf(expected, sizeof expected, "<27>errd[%d]: literal %%m\n", getpid());
    expect_sink(expected);

    errno = 0;
}

static void test_truncation(void) {
    static char big[5000];
    static char output[ASTIA_SYSLOG_RECORD_MAX + 64];
    long        length;

    memset(big, 'A', sizeof big - 1);

    big[sizeof big - 1] = '\0';

    new_sink();
    openlog("long", 0, LOG_USER);

    syslog(LOG_INFO, "%s", big);

    length = sink_read(output, sizeof output);

    CHECK(length == ASTIA_SYSLOG_RECORD_MAX);
    CHECK(length >= 4);

    if (length >= 4) {
        CHECK(memcmp(output + length - 4, "...\n", 4) == 0);
        CHECK(memchr(output, '\n', (size_t) length - 4) == NULL);
    }
}

static void test_perror(void) {
    char sink[ASTIA_SYSLOG_RECORD_MAX];
    char error[ASTIA_SYSLOG_RECORD_MAX];
    int  saved;

    new_sink();
    openlog("perr", LOG_PERROR, LOG_USER);

    stderr_capture_begin();
    syslog(LOG_ERR, "duplicate");
    stderr_capture_end(error, sizeof error);

    sink_read(sink, sizeof sink);

    CHECK(strcmp(sink, "<11>perr: duplicate\n") == 0);
    CHECK(strcmp(error, sink) == 0);

    clear_sink();

    saved = dup(STDERR_FILENO);

    CHECK(saved >= 0);
    close(STDERR_FILENO);

    syslog(LOG_ERR, "without stderr");

    if (saved >= 0) {
        CHECK(dup2(saved, STDERR_FILENO) == STDERR_FILENO);
        close(saved);
    }

    expect_sink("<11>perr: without stderr\n");
}

static void test_unavailable_sink(void) {
    char error[512];

    new_sink();
    closelog();
    strcpy(sink_path, "/tmp/astia_syslog_missing_directory/sink");

    stderr_capture_begin();
    syslog(LOG_ERR, "first");
    syslog(LOG_ERR, "second");
    stderr_capture_end(error, sizeof error);

    CHECK(strstr(error, "syslog") != NULL);
    CHECK(counted_occurrences(error, "cannot open log sink") == 1);
}

static void* log_worker(void* argument) {
    long id = (long) argument;
    int  i;

    for (i = 0; i < 50; i++) {
        syslog(LOG_INFO, "thread-%ld-%d", id, i);
    }

    return NULL;
}

static void test_concurrent_writers(void) {
    enum { thread_count = 4, records_per_thread = 50 };

    static char output[65536];
    static int  seen[thread_count][records_per_thread];

    pthread_t threads[thread_count];

    long length;
    long lines = 0;

    const char* at;

    int i;

    new_sink();
    openlog("conc", LOG_PID, LOG_DAEMON);

    for (i = 0; i < thread_count; i++) {
        CHECK(pthread_create(&threads[i], NULL, log_worker, (void*) (long) i) == 0);
    }

    for (i = 0; i < thread_count; i++) {
        CHECK(pthread_join(threads[i], NULL) == 0);
    }

    length = sink_read(output, sizeof output);

    CHECK(length > 0);

    at = output;

    while (*at != '\0') {
        const char* newline = strchr(at, '\n');

        char line[160];

        char* end;

        size_t line_length;
        long   pid;
        long   id;
        long   sequence;

        CHECK(newline != NULL);

        if (newline == NULL)
            break;

        line_length = (size_t) (newline - at);

        CHECK(line_length < sizeof line);

        if (line_length >= sizeof line)
            break;

        memcpy(line, at, line_length);

        line[line_length] = '\0';

        CHECK(strncmp(line, "<30>conc[", 9) == 0);

        pid = strtol(line + 9, &end, 10);

        CHECK(pid == getpid());
        CHECK(strncmp(end, "]: thread-", 10) == 0);

        id = strtol(end + 10, &end, 10);

        CHECK(*end == '-');

        sequence = strtol(end + 1, &end, 10);

        CHECK(*end == '\0');
        CHECK(id >= 0 && id < thread_count);
        CHECK(sequence >= 0 && sequence < records_per_thread);

        if (id >= 0 && id < thread_count && sequence >= 0 &&
            sequence < records_per_thread)
            seen[id][sequence]++;

        lines++;

        at = newline + 1;
    }

    CHECK(lines == thread_count * records_per_thread);

    for (i = 0; i < thread_count; i++) {
        int j;

        for (j = 0; j < records_per_thread; j++) {
            CHECK(seen[i][j] == 1);
        }
    }
}

int main(void) {
    CHECK(LOG_EMERG == 0 && LOG_DEBUG == 7);
    CHECK(LOG_KERN == 0 && LOG_USER == 8 && LOG_DAEMON == 24);
    CHECK(LOG_AUTH == 32 && LOG_AUTHPRIV == 80);
    CHECK(LOG_MAKEPRI(LOG_DAEMON, LOG_INFO) == 30);

    test_default_mask();
    test_priority_filtering();
    test_openlog_formatting();
    test_percent_m();
    test_truncation();
    test_perror();
    test_unavailable_sink();
    test_concurrent_writers();

    if (fails)
        printf("syslog_test: %d failure(s)\n", fails);

    return fails != 0;
}
