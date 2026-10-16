#include "device.hpp"

#include <astia/api/errno.h>

namespace astia::dev {

    long device::ioctl(u64 request, void* argp) {
        (void) request;
        (void) argp;
        return -ENOTTY;
    }

    long device::submit(io_request& request) {
        // Inline default: route to the sync read/write pair.  A device that
        // cannot finish without waiting overrides this; 0 from here would
        // mean "parked", which this path never intends.
        return request.direction() == io_direction::input
                   ? read(request.buffer(), request.length(), request.offset())
                   : write(request.buffer(), request.length(), request.offset());
    }

} // namespace astia::dev
