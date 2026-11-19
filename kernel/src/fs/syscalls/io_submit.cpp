#include "io_submit.hpp"

#include "../../sys/aio.hpp"
#include <astia/api/aio.h>

namespace astia::fs::syscall {
    i64 io_submit(u64 ring, u32 submit) {
        return sys::io_submit(reinterpret_cast<astia_rings*>(static_cast<usize>(ring)),
                              submit);
    }
}
