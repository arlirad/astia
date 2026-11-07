# Process alarm

## NAME

`alarm` - schedule a one-shot `SIGALRM` for the calling process

## SYNOPSIS

```c
#include <unistd.h>

unsigned int alarm(unsigned int seconds);
```

## DESCRIPTION

`alarm(seconds)` replaces the caller's pending alarm. A nonzero value arms a
timer in seconds; zero cancels it. The call returns the number of whole
seconds remaining on the replaced timer, rounded up, or zero when none was
active. When the timer expires, the kernel queues `SIGALRM` and wakes an
interruptible wait in the process.

Only one alarm is tracked per process. Re-arming replaces rather than queues
alarms, and a process that exits cannot receive its pending alarm.

## ERRORS

The libc wrapper returns zero and sets `errno` if the syscall fails. The
kernel rejects an unavailable current process or an invalid syscall value.

## NOTES

Alarm delivery is tick based, so expiration has timer-tick resolution. The
shared baseline's existing `munmap` scenario still reports status 8; that
baseline result is unrelated to alarm and is intentionally not changed here.
End-to-end userspace validation is also blocked when the generated startup
object (`/tmp/astia-clang-init-libc/image/crt0.o`) and baseline initrd
(`kernel/build-tests/tests/fixtures/boot_initrd.tar`) are absent. These are
generated baseline artifacts, not alarm failures; `toolchain/build-init-libc.sh`
and the kernel test build regenerate them.
