#include "internal/syslog_sink.h"

__attribute__((weak)) const char* __astia_syslog_sink_path(void) {
    return "/dev/console";
}
