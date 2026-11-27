#include "../src/daemonctl_path.h"

#include <stdio.h>

static int failures;

static void expect(const char* what, int condition) {
    if (condition)
        return;

    printf("daemonctl path test failed: %s\n", what);
    ++failures;
}

static void check_short_path(void) {
    const char*        path = "/tmp/daemon.sock";
    struct sockaddr_un address;

    size_t length = 0;

    __builtin_memset(&address, 0xff, sizeof address);

    expect("short path accepted",
           cu_daemonctl_socket_address(&address, path, &length) == 0);
    expect("short path length", length == __builtin_strlen(path));
    expect("short path family", address.sun_family == AF_UNIX);
    expect("short path contents", __builtin_strcmp(address.sun_path, path) == 0);
    expect("short path terminator", address.sun_path[__builtin_strlen(path)] == '\0');
    expect("short path trailing bytes zero",
           address.sun_path[__builtin_strlen(path) + 1] == '\0');
}

static void check_exact_fit(void) {
    struct sockaddr_un address;

    char   path[sizeof address.sun_path];
    size_t capacity = sizeof address.sun_path;
    size_t length   = 0;

    __builtin_memset(path, 'a', capacity - 1);

    path[capacity - 1] = '\0';

    __builtin_memset(&address, 0xff, sizeof address);

    expect("exact fit accepted",
           cu_daemonctl_socket_address(&address, path, &length) == 0);
    expect("exact fit length", length == capacity - 1);
    expect("exact fit family", address.sun_family == AF_UNIX);
    expect("exact fit contents", __builtin_memcmp(address.sun_path, path, capacity) == 0);
    expect("exact fit terminator", address.sun_path[capacity - 1] == '\0');
}

static void check_overlong(void) {
    struct sockaddr_un address;
    struct sockaddr_un untouched;

    char path[sizeof address.sun_path + 1];

    __builtin_memset(path, 'b', sizeof path - 1);

    path[sizeof path - 1] = '\0';

    __builtin_memset(&address, 0x5a, sizeof address);
    __builtin_memcpy(&untouched, &address, sizeof address);

    errno = 0;

    expect("overlong path rejected",
           cu_daemonctl_socket_address(&address, path, NULL) == -1);
    expect("overlong path errno", errno == ENAMETOOLONG);
    expect("overlong path leaves address untouched",
           __builtin_memcmp(&address, &untouched, sizeof address) == 0);
}

int main(void) {
    check_short_path();
    check_exact_fit();
    check_overlong();

    if (failures != 0)
        return 1;

    printf("daemonctl path tests ok\n");

    return 0;
}
