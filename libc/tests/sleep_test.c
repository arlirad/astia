#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

extern int     sleep_host_calls;
extern int64_t sleep_host_sec;
extern int64_t sleep_host_nsec;
extern long    sleep_host_result;
extern int64_t sleep_host_remaining_sec;
extern int64_t sleep_host_remaining_nsec;

int main(void) {
    struct timespec request = {3, 4};
    struct timespec remaining = {9, 9};

    if (nanosleep(NULL, &remaining) != -1 || errno != EINVAL || sleep_host_calls != 0)
        return 1;
    if (nanosleep(&request, &remaining) != 0 || sleep_host_calls != 1 ||
        sleep_host_sec != 3 || sleep_host_nsec != 4 || remaining.tv_sec != 0 ||
        remaining.tv_nsec != 0)
        return 1;
    sleep_host_result         = -EINTR;
    sleep_host_remaining_sec  = 2;
    sleep_host_remaining_nsec = 3;
    if (nanosleep(&request, &remaining) != -1 || errno != EINTR ||
        sleep_host_calls != 2 || remaining.tv_sec != 2 || remaining.tv_nsec != 3)
        return 1;
    sleep_host_result = 0;
    if (sleep(7) != 0 || sleep_host_calls != 3 || sleep_host_sec != 7 ||
        sleep_host_nsec != 0)
        return 1;
    request.tv_nsec = 1000000000L;
    if (nanosleep(&request, NULL) != -1 || errno != EINVAL || sleep_host_calls != 3)
        return 1;
    puts("sleep tests ok");
    return 0;
}
