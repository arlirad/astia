#include <astia/log.hpp>
#include <astia/module.hpp>

namespace {
    using logs = astia::log::context<"eth">;
} // namespace

// The loader runs module_main() on entry and teardown through .fini_array.
// module_exit is the explicit exit entrypoint for the module-lifecycle work;
// this scaffold owns no network state yet.
int module_main() {
    logs::info("ready");

    return 0;
}

int module_exit() {
    logs::info("stopped");

    return 0;
}
