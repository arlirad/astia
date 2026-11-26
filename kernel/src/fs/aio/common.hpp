#pragma once

#include <astia/api/errno.h>

#include "../../io/aio/dispatch.hpp"
#include "../../io/aio/memory.hpp"
#include "../../io/aio/ring.hpp"
#include "../../proc/mapping.hpp"
#include "../../proc/proc.hpp"
#include "../mapping.hpp"

namespace astia::io::aio::operations {
    using namespace ::astia::io::aio::ring;
    using ::astia::io::aio::dispatch::file_for;
    using ::astia::io::aio::dispatch::parent_writable;
    using ::astia::io::aio::dispatch::resolve_at;
} // namespace astia::io::aio::operations
