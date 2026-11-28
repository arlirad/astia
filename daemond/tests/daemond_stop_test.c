#include "daemond.h"
#include <daemon_protocol.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum {
    REPLY_BYTES          = 4096,
    START_TIMEOUT_MS     = 5000,
    REPLY_TIMEOUT_MS     = 10000,
    DAEMON_TIMEOUT_MS    = 10000,
    POLL_INTERVAL_MS     = 20,
    STOP_GRACE_MARGIN_MS = 1500
};

static int failures;

static void check(int condition, const char* expression, int line) {
    if (condition)
        return;

    fprintf(stderr, "FAIL daemond_stop_test.c:%d: %s\n", line, expression);

    ++failures;
}

#define CHECK(condition) check((condition) != 0, #condition, __LINE__)

static long long monotonic_ms(void) {
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
        return 0;

    return (long long) now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void sleep_ms(long milliseconds) {
    struct timespec delay;

    delay.tv_sec  = milliseconds / 1000;
    delay.tv_nsec = (milliseconds % 1000) * 1000000L;

    (void) nanosleep(&delay, 0);
}

static int service_main(int argc, char** argv) {
    if (argc < 2 || (strcmp(argv[1], "ignore") && strcmp(argv[1], "graceful")))
        return -1;

    if (!strcmp(argv[1], "ignore")) {
        signal(SIGTERM, SIG_IGN);
        signal(SIGINT, SIG_IGN);
    }

    for (;;) {
        sleep(1);
    }
}

static int try_connect(void) {
    struct sockaddr_un address;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (fd < 0)
        return -1;

    memset(&address, 0, sizeof address);

    address.sun_family = AF_UNIX;

    strcpy(address.sun_path, DAEMON_SOCKET_PATH);

    if (connect(fd, (struct sockaddr*) &address,
                (unsigned) (offsetof(struct sockaddr_un, sun_path) +
                            strlen(DAEMON_SOCKET_PATH) + 1)) < 0) {
        close(fd);

        return -1;
    }

    return fd;
}

static int connect_control(long timeout_ms) {
    long long deadline = monotonic_ms() + timeout_ms;

    for (;;) {
        int fd = try_connect();
        if (fd >= 0)
            return fd;

        if (monotonic_ms() >= deadline)
            return -1;

        sleep_ms(POLL_INTERVAL_MS);
    }
}

static int send_all_flags(int fd, const char* text, size_t size, int flags) {
    while (size) {
        long written = send(fd, text, size, flags);
        if (written <= 0)
            return -1;

        text += written;
        size -= (size_t) written;
    }

    return 0;
}

static int send_all(int fd, const char* text) {
    return send_all_flags(fd, text, strlen(text), 0);
}

static int read_reply(int fd, char* buffer, size_t size) {
    size_t used = 0;

    while (used + 1 < size) {
        struct pollfd ready = {fd, POLLIN, 0};

        int result = poll(&ready, 1, REPLY_TIMEOUT_MS);

        if (result <= 0)
            return -1;

        long received = recv(fd, buffer + used, size - 1 - used, 0);

        if (received < 0 && errno == EINTR)
            continue;

        if (received <= 0)
            break;

        used += (size_t) received;
        buffer[used] = '\0';
    }

    return used ? 0 : -1;
}

static int request(const char* text, char* reply, size_t size, long long* elapsed_ms) {
    int fd = connect_control(START_TIMEOUT_MS);
    if (fd < 0)
        return -1;

    *elapsed_ms = monotonic_ms();

    int result = send_all(fd, text);

    if (result == 0)
        result = read_reply(fd, reply, size);

    *elapsed_ms = monotonic_ms() - *elapsed_ms;

    close(fd);

    return result;
}

static int parse_service_pid(const char* reply) {
    const char* cursor = reply;

    while ((cursor = strstr(cursor, "pid=")) != NULL) {
        int pid = atoi(cursor + 4);
        if (pid > 0)
            return pid;

        cursor += 4;
    }

    return -1;
}

static int wait_for_service(int previous_pid) {
    long long deadline = monotonic_ms() + START_TIMEOUT_MS;

    for (;;) {
        char reply[REPLY_BYTES];

        int fd = try_connect();

        if (fd >= 0) {
            int pid = -1;

            if (send_all(fd, "STATUS\n") == 0 && read_reply(fd, reply, sizeof reply) == 0)
                pid = parse_service_pid(reply);

            close(fd);

            if (pid > 0 && pid != previous_pid)
                return pid;
        }

        if (monotonic_ms() >= deadline)
            return -1;

        sleep_ms(POLL_INTERVAL_MS);
    }
}

static int wait_for_child(int pid, long timeout_ms, int* status) {
    long long deadline = monotonic_ms() + timeout_ms;

    for (;;) {
        int result = waitpid(pid, status, WNOHANG);
        if (result == pid)
            return 1;

        if (result < 0 && errno != EINTR)
            return 0;

        if (monotonic_ms() >= deadline)
            return 0;

        sleep_ms(POLL_INTERVAL_MS);
    }
}

static void remove_case(const char* directory) {
    char unit_path[PATH_MAX];

    snprintf(unit_path, sizeof unit_path, "%s/unit.yaml", directory);

    unlink(unit_path);
    rmdir(directory);
}

static int prepare_case(const char* self, const char* mode, char* rc_path, size_t rc_size,
                        char* service_path, size_t service_size) {
    snprintf(rc_path, rc_size, "/tmp/astia-daemond-stop-%d-%s", (int) getpid(), mode);

    /* The supervisor's STATUS line is bounded, so keep the unit path short
     * instead of exposing the build-tree depth in the service name. */
    snprintf(service_path, service_size, "/tmp/astia-daemond-svc-%d-%s", (int) getpid(),
             mode);

    remove_case(rc_path);
    unlink(service_path);
    unlink(DAEMON_SOCKET_PATH);
    unlink(DAEMON_SOCKET_FALLBACK);

    if (mkdir(rc_path, 0700) < 0)
        return -1;

    if (symlink(self, service_path) < 0) {
        remove_case(rc_path);

        return -1;
    }

    char unit_path[PATH_MAX];

    snprintf(unit_path, sizeof unit_path, "%s/unit.yaml", rc_path);

    FILE* file = fopen(unit_path, "w");
    if (!file) {
        unlink(service_path);
        remove_case(rc_path);

        return -1;
    }

    fprintf(file, "exec: %s\nargs:\n  - %s\n", service_path,
            strcmp(mode, "graceful") ? "ignore" : "graceful");
    fclose(file);

    return 0;
}

static void run_case(const char* self, const char* mode) {
    char      rc_path[PATH_MAX];
    char      service_path[PATH_MAX];
    char      reply[REPLY_BYTES] = "";
    long long elapsed            = 0;
    int       service_pid;
    int       daemon;
    int       exited;
    int       status = 0;
    int       prepared;

    prepared = prepare_case(self, mode, rc_path, sizeof rc_path, service_path,
                            sizeof service_path);

    CHECK(prepared == 0);

    if (prepared < 0)
        return;

    daemon = fork();
    if (daemon == 0) {
        int result = astia_daemon_run(rc_path);

        _exit(result == 0 ? 0 : 1);
    }

    CHECK(daemon > 0);

    if (daemon <= 0)
        return;

    service_pid = wait_for_service(0);

    CHECK(service_pid > 0);

    if (service_pid > 0 && !strcmp(mode, "restart")) {
        int restarted;

        CHECK(request("RESTART\n", reply, sizeof reply, &elapsed) == 0);
        CHECK(!strcmp(reply, "OK restarting\n"));
        CHECK(elapsed >= STOP_GRACE_MARGIN_MS);

        restarted = wait_for_service(service_pid);

        CHECK(restarted > 0);
        CHECK(kill(service_pid, 0) < 0 && errno == ESRCH);

        service_pid = restarted;
    }

    if (service_pid > 0 && strcmp(mode, "restart")) {
        CHECK(request("STOP\n", reply, sizeof reply, &elapsed) == 0);
        CHECK(!strcmp(reply, "OK stopped\n"));

        if (strcmp(mode, "graceful"))
            CHECK(elapsed >= STOP_GRACE_MARGIN_MS);
        else
            CHECK(elapsed < STOP_GRACE_MARGIN_MS);
    }

    if (service_pid > 0 && !strcmp(mode, "restart")) {
        CHECK(request("STOP\n", reply, sizeof reply, &elapsed) == 0);
        CHECK(!strcmp(reply, "OK stopped\n"));
    }

    if (service_pid > 0)
        CHECK(kill(service_pid, 0) < 0 && errno == ESRCH);

    exited = wait_for_child(daemon, DAEMON_TIMEOUT_MS, &status);

    CHECK(exited);

    if (!exited) {
        kill(daemon, SIGKILL);
        (void) wait_for_child(daemon, 1000, &status);
    }
    else {
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }

    fprintf(stderr, "daemond_stop_test: %s case done\n", mode);

    unlink(service_path);
    remove_case(rc_path);
    unlink(DAEMON_SOCKET_PATH);
    unlink(DAEMON_SOCKET_FALLBACK);
}

static int prepare_protocol_case(char* rc_path, size_t rc_size) {
    snprintf(rc_path, rc_size, "/tmp/astia-daemond-protocol-%d", (int) getpid());

    remove_case(rc_path);
    unlink(DAEMON_SOCKET_PATH);
    unlink(DAEMON_SOCKET_FALLBACK);

    return mkdir(rc_path, 0700);
}

static void run_protocol_case(void) {
    char      rc_path[PATH_MAX];
    char      reply[REPLY_BYTES] = "";
    char      oversized[DAEMON_PROTOCOL_MAX + 64];
    long long elapsed = 0;
    int       daemon;
    int       exited;
    int       fd;
    int       status   = 0;
    const int prepared = prepare_protocol_case(rc_path, sizeof rc_path);

    CHECK(prepared == 0);

    if (prepared < 0)
        return;

    daemon = fork();
    if (daemon == 0) {
        int result = astia_daemon_run(rc_path);

        _exit(result == 0 ? 0 : 1);
    }

    CHECK(daemon > 0);

    if (daemon <= 0)
        return;

    fd = connect_control(START_TIMEOUT_MS);

    CHECK(fd >= 0);

    if (fd >= 0) {
        CHECK(send_all(fd, "ST") == 0);
        sleep_ms(100);
        CHECK(send_all(fd, "ATUS\n") == 0);
        CHECK(read_reply(fd, reply, sizeof reply) == 0);
        CHECK(!strcmp(reply, "OK services=0\n"));

        close(fd);
    }

    fd = connect_control(START_TIMEOUT_MS);

    CHECK(fd >= 0);

    if (fd >= 0) {
        reply[0] = '\0';

        CHECK(send_all(fd, "STATUS") == 0);
        CHECK(shutdown(fd, SHUT_WR) == 0);
        CHECK(read_reply(fd, reply, sizeof reply) == 0);
        CHECK(!strcmp(reply, "OK services=0\n"));

        close(fd);
    }

    memset(oversized, 'A', sizeof oversized - 1);

    oversized[sizeof oversized - 1] = '\n';

    fd = connect_control(START_TIMEOUT_MS);

    CHECK(fd >= 0);

    if (fd >= 0) {
        reply[0] = '\0';

        CHECK(send_all_flags(fd, oversized, sizeof oversized, MSG_NOSIGNAL) == 0);
        CHECK(read_reply(fd, reply, sizeof reply) == 0);
        CHECK(!strcmp(reply, "ERR malformed request\n"));

        close(fd);
    }

    CHECK(request("STOP\n", reply, sizeof reply, &elapsed) == 0);
    CHECK(!strcmp(reply, "OK stopped\n"));

    exited = wait_for_child(daemon, DAEMON_TIMEOUT_MS, &status);

    CHECK(exited);

    if (!exited) {
        kill(daemon, SIGKILL);
        (void) wait_for_child(daemon, 1000, &status);
    }
    else {
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }

    fprintf(stderr, "daemond_stop_test: protocol case done\n");

    remove_case(rc_path);
    unlink(DAEMON_SOCKET_PATH);
    unlink(DAEMON_SOCKET_FALLBACK);
}

int main(int argc, char** argv) {
    char self[PATH_MAX];
    int  mode = service_main(argc, argv);

    if (mode >= 0)
        return mode;

    if (!realpath(argv[0], self)) {
        fprintf(stderr, "daemond_stop_test: cannot resolve %s\n", argv[0]);

        return 1;
    }

    run_protocol_case();
    run_case(self, "graceful");
    run_case(self, "ignore");
    run_case(self, "restart");

    if (failures) {
        fprintf(stderr, "daemond_stop_test: %d failure(s)\n", failures);

        return 1;
    }

    printf("daemond_stop_test: passed\n");

    return 0;
}
