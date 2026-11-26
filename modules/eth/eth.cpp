#include <astia/core/task.hpp>
#include <astia/log.hpp>
#include <astia/module.hpp>

namespace {
    using logs = astia::log::context<"eth">;
} // namespace

// The module lifecycle is task-shaped: entry is a coroutine and teardown runs
// through the validated .fini_array path. This scaffold owns no network state
// yet.
astia::core::task<void> module_main() {
    logs::info("ready");

    co_return astia::error_t::enone;
}
