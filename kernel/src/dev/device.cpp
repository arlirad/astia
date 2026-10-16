#include "device.hpp"

#include <astia/api/errno.h>

namespace astia::dev {

    auto device::ioctl(u64 request, void* argp) -> long {
        (void) request;
        (void) argp;
        return -ENOTTY;
    }

} // namespace astia::dev
