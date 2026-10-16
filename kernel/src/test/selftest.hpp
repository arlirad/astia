#pragma once

namespace astia::test {

    /// Hardware-backed kernel integration checks.  This is linked only
    /// into astiakrn_test, never into the production kernel entrypoint.
    bool run();

} // namespace astia::test
