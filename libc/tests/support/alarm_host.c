#include "internal/process_port.h"

int          alarm_host_calls;
unsigned int alarm_host_seconds;
long         alarm_host_result;

long __astia_process_alarm(unsigned int seconds) {
    ++alarm_host_calls;

    alarm_host_seconds = seconds;

    return alarm_host_result;
}
