#pragma once

#include <astia/api/errno.h>

#include "../../proc/mapping.hpp"
#include "../dispatch.hpp"
#include "../handler.hpp"

namespace astia::io::aio::operations {
    using namespace ::astia::io::aio::ring;
    using namespace ::astia::io::aio::dispatch;
} // namespace astia::io::aio::operations
