#include "device.hpp"

#include <astia/api/errno.h>

namespace astia::dev {

    u32 device::poll(u32 events, u32) { return events & (POLLIN | POLLOUT); }

    long device::ioctl(u64 request, void* argp) {
        (void) request;
        (void) argp;
        return -ENOTTY;
    }

    long device::submit(io_request& request) {
        // Inline default: route to the sync read/write pair.  A device that
        // cannot finish without waiting overrides this and parks.  The base
        // path is synchronous, so a zero result is a completed operation,
        // not a parked one: /dev/null uses zero to report EOF.  Async
        // devices (notably tty) override submit() and explicitly leave the
        // request incomplete when they need an interrupt-driven resume.
        const long result = request.direction() == io_direction::input
                                ? read(request.buffer(), request.length(), request.offset())
                                : write(request.buffer(), request.length(), request.offset());
        if (result == 0) {
            request.complete(true);
        }
        return result;
    }

} // namespace astia::dev
