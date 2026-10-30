#include "io_setup.hpp"

#include "../../sys/aio.hpp"
#include "../../sys/user_mem.hpp"
#include <astia/api/aio.h>
#include <astia/api/errno.h>

namespace astia::fs::syscall {
    i64 io_setup(u32 entries, u32 flags, u64 out) {
        const sys::user_range ring_out =
            sys::user_range::bytes(out, sizeof(astia_rings*));
        if (!sys::user_range_ok(ring_out))
            return -EFAULT;
        return sys::io_setup(entries, flags,
                             reinterpret_cast<astia_rings**>(ring_out.address()));
    }
}
