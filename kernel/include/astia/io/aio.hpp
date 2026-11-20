#pragma once

#include <astia/api/aio.h>
#include <astia/common.hpp>

namespace astia::fs {
    class file;
}

namespace astia::proc {
    class process;
}

namespace astia::io::aio {

    using io_device = fs::file;

} // namespace astia::io::aio
