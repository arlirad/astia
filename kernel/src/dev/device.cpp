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
        // cannot finish without waiting overrides this and parks; a 0 from
        // read() here is that park (see device::read), never the completion
        // a filesystem node's default reports — a device has no end of file.
        return request.direction() == io_direction::input
                   ? read(request.buffer(), request.length(), request.offset())
                   : write(request.buffer(), request.length(), request.offset());
    }

} // namespace astia::dev
