#pragma once

namespace astia::test {

    /// Hardware-backed kernel integration checks.  This is linked only
    /// into astiakrn_test, never into the production kernel entrypoint.
    auto run() -> bool;

} // namespace astia::test
