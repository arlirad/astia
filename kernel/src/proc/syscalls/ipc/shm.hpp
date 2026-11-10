#pragma once

namespace astia::proc::syscall {
    long umask(int mode);
    long shm_open(unsigned long name, int flags, unsigned int mode);
    long shm_unlink(unsigned long name);
}
