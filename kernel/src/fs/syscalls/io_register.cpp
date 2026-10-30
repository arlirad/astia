#include "io_register.hpp"

#include <astia/api/errno.h>
namespace astia::fs::syscall {
    i64 io_register() {
        return -ENOSYS;
    }
}
