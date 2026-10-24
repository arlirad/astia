#include "io_destroy.hpp"
#include "../../sys/aio.hpp"
#include <astia/api/aio.h>
namespace astia::fs::syscall {
    i64 io_destroy(u64 ring) {
        return sys::io_destroy(reinterpret_cast<astia_rings*>(static_cast<usize>(ring)));
    }
}
