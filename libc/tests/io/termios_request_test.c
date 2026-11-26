#include "internal/file_port.h"
#include <sys/ioctl.h>
#include <sys/syscall.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>

extern long syscall(long number, ...);

_Static_assert(TCGETS == 0x5401, "TCGETS must match the termios ABI");
_Static_assert(TCSETS == 0x5402, "TCSETS must match the termios ABI");
_Static_assert(TCSETSW == 0x5403, "TCSETSW must match the termios ABI");
_Static_assert(TCSETSF == 0x5404, "TCSETSF must match the termios ABI");
_Static_assert(TCFLSH == 0x540b, "TCFLSH must match the termios ABI");
_Static_assert(TCIFLUSH == 0, "TCIFLUSH must match the termios ABI");
_Static_assert(TCOFLUSH == 1, "TCOFLUSH must match the termios ABI");
_Static_assert(TCIOFLUSH == 2, "TCIOFLUSH must match the termios ABI");
_Static_assert(sizeof(struct termios) == 36,
               "struct termios size defines the TCGETS/TCSETS payload");

static int           failures;
static unsigned      backend_calls;
static int           last_fd;
static uint64_t      last_request;
static size_t        last_length;
static void*         last_arg;
static long          backend_result;
static unsigned char termios_bytes[sizeof(struct termios)];
static int           termios_bytes_valid;
static int           selector;
static int           selector_valid;

static const struct termios backend_termios = {
    .c_iflag  = ICRNL | IXON,
    .c_oflag  = OPOST | ONLCR,
    .c_cflag  = CS8 | CREAD | CLOCAL,
    .c_lflag  = ISIG | ICANON | ECHO,
    .c_line   = 0,
    .c_cc     = {[VINTR] = 3, [VEOF] = 4, [VMIN] = 1},
    .c_ispeed = B9600,
    .c_ospeed = B9600,
};

long __astia_fio_ioctl(int fd, uint64_t request, void* arg, size_t length) {
    ++backend_calls;

    last_fd             = fd;
    last_request        = request;
    last_length         = length;
    last_arg            = arg;
    termios_bytes_valid = 0;
    selector_valid      = 0;

    if (request == TCGETS || request == TCSETS || request == TCSETSW ||
        request == TCSETSF) {
        if (arg != NULL) {
            memcpy(termios_bytes, arg, sizeof(termios_bytes));

            termios_bytes_valid = 1;

            if (request == TCGETS && backend_result == 0)
                memcpy(arg, &backend_termios, sizeof(backend_termios));
        }
    }
    else if (request == TCFLSH && arg != NULL) {
        selector       = *(const int*) arg;
        selector_valid = 1;
    }

    return backend_result;
}

static void report_failure(const char* file, int line, const char* expression) {
    char message[256];
    int  length =
        snprintf(message, sizeof(message), "FAIL %s:%d: %s\n", file, line, expression);
    size_t count;

    if (length <= 0)
        return;

    count = (size_t) length;

    if (count >= sizeof(message))
        count = sizeof(message) - 1;

    syscall(SYS_write, 2, message, count);
}

#define CHECK(condition)                                    \
    do {                                                    \
        if (!(condition)) {                                 \
            report_failure(__FILE__, __LINE__, #condition); \
            ++failures;                                     \
        }                                                   \
    } while (0)

static void reset_mock(long result) {
    backend_calls       = 0;
    last_fd             = -1;
    last_request        = 0;
    last_length         = 0;
    last_arg            = NULL;
    backend_result      = result;
    termios_bytes_valid = 0;
    selector            = -1;
    selector_valid      = 0;

    memset(termios_bytes, 0, sizeof(termios_bytes));
}

static void check_termios_request(int fd, uint64_t request, const void* arg,
                                  const struct termios* expected) {
    CHECK(backend_calls == 1);
    CHECK(last_fd == fd);
    CHECK(last_request == request);
    CHECK(last_length == sizeof(struct termios));
    CHECK(last_arg == arg);
    CHECK(termios_bytes_valid == 1);

    if (expected != NULL)
        CHECK(memcmp(termios_bytes, expected, sizeof(*expected)) == 0);
}

static void check_flush_request(int fd, int expected_selector) {
    CHECK(backend_calls == 1);
    CHECK(last_fd == fd);
    CHECK(last_request == TCFLSH);
    CHECK(last_length == sizeof(int));
    CHECK(last_arg != NULL);
    CHECK(selector_valid == 1);
    CHECK(selector == expected_selector);
}

static void check_tcgetattr_maps_to_tcgets(void) {
    struct termios value;

    memset(&value, 0xa5, sizeof(value));
    reset_mock(0);

    errno = 0;

    CHECK(tcgetattr(7, &value) == 0);
    check_termios_request(7, TCGETS, &value, NULL);
    CHECK(memcmp(&value, &backend_termios, sizeof(value)) == 0);
}

static void check_tcsetattr_maps_actions(void) {
    static const struct {
        int           action;
        unsigned long request;
    } cases[] = {
        {TCSANOW, TCSETS},
        {TCSADRAIN, TCSETSW},
        {TCSAFLUSH, TCSETSF},
    };

    struct termios expected;
    struct termios value = {
        .c_iflag  = IGNBRK | ICRNL,
        .c_oflag  = OPOST,
        .c_cflag  = CS7 | PARENB,
        .c_lflag  = ECHO | ECHOE,
        .c_line   = 0,
        .c_cc     = {[VERASE] = 8, [VKILL] = 21},
        .c_ispeed = B1200,
        .c_ospeed = B2400,
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        expected = value;

        reset_mock(0);

        errno = 0;

        CHECK(tcsetattr(9, cases[i].action, &value) == 0);
        check_termios_request(9, cases[i].request, &value, &expected);
    }
}

static void check_tcsetattr_rejects_invalid_action(void) {
    static const int invalid_actions[] = {-1, 3, 99};

    struct termios value;

    int error;
    int rc;

    memset(&value, 0, sizeof(value));

    for (size_t i = 0; i < sizeof(invalid_actions) / sizeof(invalid_actions[0]); ++i) {
        reset_mock(0);

        errno = 0;
        rc    = tcsetattr(9, invalid_actions[i], &value);
        error = errno;

        CHECK(rc == -1);
        CHECK(error == EINVAL);
        CHECK(backend_calls == 0);
    }
}

static void check_tcflush_maps_selectors(void) {
    static const int selectors[] = {TCIFLUSH, TCOFLUSH, TCIOFLUSH};

    for (size_t i = 0; i < sizeof(selectors) / sizeof(selectors[0]); ++i) {
        reset_mock(0);

        errno = 0;

        CHECK(tcflush(5, selectors[i]) == 0);
        check_flush_request(5, selectors[i]);
    }
}

static void check_tcflush_rejects_invalid_selector(void) {
    static const int invalid_selectors[] = {-1, 3, 99};
    int              error;
    int              rc;

    for (size_t i = 0; i < sizeof(invalid_selectors) / sizeof(invalid_selectors[0]);
         ++i) {
        reset_mock(0);

        errno = 0;
        rc    = tcflush(5, invalid_selectors[i]);
        error = errno;

        CHECK(rc == -1);
        CHECK(error == EINVAL);
        CHECK(backend_calls == 0);
    }
}

static void check_tcflush_rejects_negative_fd(void) {
    int error;
    int rc;

    reset_mock(0);

    errno = 0;
    rc    = tcflush(-1, TCIFLUSH);
    error = errno;

    CHECK(rc == -1);
    CHECK(error == EBADF);
    CHECK(backend_calls == 0);
}

static void check_backend_enotty_folds_through(void) {
    struct termios expected;
    struct termios value;

    int error;
    int rc;

    memset(&value, 0, sizeof(value));

    reset_mock(-ENOTTY);

    errno = 0;
    rc    = tcgetattr(3, &value);
    error = errno;

    CHECK(rc == -1);
    CHECK(error == ENOTTY);
    check_termios_request(3, TCGETS, &value, NULL);

    expected = value;

    reset_mock(-ENOTTY);

    errno = 0;
    rc    = tcsetattr(3, TCSANOW, &value);
    error = errno;

    CHECK(rc == -1);
    CHECK(error == ENOTTY);
    check_termios_request(3, TCSETS, &value, &expected);

    reset_mock(-ENOTTY);

    errno = 0;
    rc    = tcflush(3, TCIFLUSH);
    error = errno;

    CHECK(rc == -1);
    CHECK(error == ENOTTY);
    check_flush_request(3, TCIFLUSH);
}

static void check_ioctl_rejects_unknown_request(void) {
    struct termios value;

    int error;
    int rc;

    memset(&value, 0, sizeof(value));
    reset_mock(0);

    errno = 0;
    rc    = ioctl(4, 0xdeadbeefUL, NULL);
    error = errno;

    CHECK(rc == -1);
    CHECK(error == ENOTTY);
    CHECK(backend_calls == 0);
}

static void check_ioctl_rejects_negative_fd(void) {
    struct termios value;

    int error;
    int rc;

    memset(&value, 0, sizeof(value));
    reset_mock(0);

    errno = 0;
    rc    = ioctl(-1, TCGETS, &value);
    error = errno;

    CHECK(rc == -1);
    CHECK(error == EBADF);
    CHECK(backend_calls == 0);
}

int main(void) {
    check_tcgetattr_maps_to_tcgets();
    check_tcsetattr_maps_actions();
    check_tcsetattr_rejects_invalid_action();
    check_tcflush_maps_selectors();
    check_tcflush_rejects_invalid_selector();
    check_tcflush_rejects_negative_fd();
    check_backend_enotty_folds_through();
    check_ioctl_rejects_unknown_request();
    check_ioctl_rejects_negative_fd();

    return failures != 0;
}
