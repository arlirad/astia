#include <sys/types.h>

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define read login_test_read
#define setsid login_test_setsid
#define getpgrp login_test_getpgrp
#define tcsetpgrp login_test_tcsetpgrp
ssize_t login_test_read(int fd, void* buffer, size_t count);
int     login_test_setsid(void);
pid_t   login_test_getpgrp(void);
int     login_test_tcsetpgrp(int fd, int process_group);
#define main login_program_main
#include "../src/login.c"
#undef main
#undef read
#undef setsid
#undef getpgrp
#undef tcsetpgrp

static const char* input;
static size_t      input_offset;
static int         interrupt_pending;

static int   session_setsid_status;
static pid_t session_group;
static int   session_tcsetpgrp_status;
static int   session_tcsetpgrp_fd;
static int   session_tcsetpgrp_group;
static int   session_tcsetpgrp_calls;

ssize_t login_test_read(int fd, void* buffer, size_t count) {
    (void) fd;
    (void) count;

    if (interrupt_pending) {
        interrupt_pending = 0;
        errno             = EINTR;

        return -1;
    }

    if (input[input_offset] == '\0')
        return 0;

    ((char*) buffer)[0] = input[input_offset++];

    return 1;
}

int login_test_setsid(void) {
    return session_setsid_status;
}

pid_t login_test_getpgrp(void) {
    return session_group;
}

int login_test_tcsetpgrp(int fd, int process_group) {
    ++session_tcsetpgrp_calls;

    session_tcsetpgrp_fd    = fd;
    session_tcsetpgrp_group = process_group;

    return session_tcsetpgrp_status;
}

static int check_name(const char* label, const char* script, int interrupt,
                      unsigned long size, int expected_status,
                      const char* expected_name) {
    char name[64];

    input             = script;
    input_offset      = 0;
    interrupt_pending = interrupt;

    int status = read_name(name, size);

    if (status != expected_status ||
        (expected_name != NULL && strcmp(name, expected_name) != 0)) {
        printf("login read test failed: %s (status=%d name=[%s])\n", label, status,
               status == 0 ? name : "");

        return 1;
    }

    return 0;
}

static int check_session(const char* label, int setsid_status, pid_t group,
                         int tcsetpgrp_status, enum login_session_result expected,
                         int expected_calls) {
    session_setsid_status    = setsid_status;
    session_group            = group;
    session_tcsetpgrp_status = tcsetpgrp_status;
    session_tcsetpgrp_fd     = -1;
    session_tcsetpgrp_group  = -1;
    session_tcsetpgrp_calls  = 0;

    enum login_session_result result = establish_session(0);

    if (result != expected || session_tcsetpgrp_calls != expected_calls ||
        (expected_calls != 0 &&
         (session_tcsetpgrp_fd != 0 || session_tcsetpgrp_group != group))) {
        printf("login session test failed: %s (result=%d calls=%d)\n", label, result,
               session_tcsetpgrp_calls);

        return 1;
    }

    return 0;
}

int main(void) {
    if (check_name("interrupted then split", "root\n", 1, 64, 0, "root") ||
        check_name("partial reads", "alice\r", 0, 64, 0, "alice") ||
        check_name("eof without newline", "bob", 0, 64, 0, "bob") ||
        check_name("empty input", "", 0, 64, -1, NULL) ||
        check_name("empty line", "\n", 0, 64, -1, NULL) ||
        check_name("colon rejected", "a:b\n", 0, 64, -1, NULL) ||
        check_name("bounded buffer", "abcdefghijklmnop\n", 0, 8, -1, NULL))
        return 1;

    if (check_session("session established", 0, 42, 0, LOGIN_SESSION_OK, 1) ||
        check_session("setsid failure", -1, 42, 0, LOGIN_SESSION_SETSID_FAILED, 0) ||
        check_session("getpgrp failure", 0, -1, 0, LOGIN_SESSION_GETPGRP_FAILED, 0) ||
        check_session("tcsetpgrp failure", 0, 42, -1, LOGIN_SESSION_TCSETPGRP_FAILED, 1))
        return 1;

    printf("login read test ok\n");

    return 0;
}
