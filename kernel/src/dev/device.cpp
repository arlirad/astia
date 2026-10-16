#include "device.hpp"

#include <astia/api/errno.h>

namespace astia::dev {

    long device::ioctl(u64 request, void* argp) {
        (void) request;
        (void) argp;
        return -ENOTTY;
    }

} // namespace astia::dev
