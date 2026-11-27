#pragma once

namespace astia::net {
    /// Common tag for wire-header structs.  It carries no data; protocol
    /// headers derive from it so shared helpers can name "a header" without
    /// caring about the concrete layout.
    struct header {};
} // namespace astia::net
