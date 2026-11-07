#include <errno.h>
#include <stdio.h>
#include <unistd.h>

extern int          alarm_host_calls;
extern unsigned int alarm_host_seconds;
extern long         alarm_host_result;

int main(void) {
    alarm_host_result = 7;
    if (alarm(5) != 7 || alarm_host_calls != 1 || alarm_host_seconds != 5)
        return 1;
    alarm_host_result = 0;
    if (alarm(0) != 0 || alarm_host_calls != 2 || alarm_host_seconds != 0)
        return 1;
    alarm_host_result = -EINTR;
    if (alarm(1) != 0 || errno != EINTR || alarm_host_calls != 3 ||
        alarm_host_seconds != 1)
        return 1;
    puts("alarm tests ok");
    return 0;
}
