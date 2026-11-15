#include "selftest_arch.hpp"

#include <astia/log.hpp>

namespace astia::test {

    bool run_arch_checks() {
        astia::log::context<"selftest">::error(
            "AArch64 kernel self-tests are unsupported by the bring-up port");
        return false;
    }

}
