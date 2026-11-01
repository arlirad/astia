#include "io_setup.hpp"

#include "../../sys/aio.hpp"
#include "../../sys/user_mem.hpp"
#include <astia/api/aio.h>
#include <astia/api/errno.h>

namespace astia::fs::syscall {
    i64 io_setup(u32 entries, u32 flags, u64 out) {
        const auto ring_out = sys::user_ptr<astia_rings*>(out).validate();
        if (!ring_out)
            return -EFAULT;
        return sys::io_setup(entries, flags,
                             ring_out.value.get());
    }
}
