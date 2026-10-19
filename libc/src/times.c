#include <sys/time.h>
#include <time.h>

clock_t times(struct tms* result) {
    if (result != 0) {
        result->tms_utime = 0;
        result->tms_stime = 0;
        result->tms_cutime = 0;
        result->tms_cstime = 0;
    }
    return 0;
}
