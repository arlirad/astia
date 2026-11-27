#include <astia/core/task.hpp>
#include <astia/log.hpp>
#include <astia/module.hpp>
#include <astia/net/link_driver.hpp>

#include "link/driver.hpp"

namespace {
    using logs = astia::log::context<"eth">;

    astia::eth::driver factory{};

    // The async module ABI runs fini handlers synchronously inside the exit
    // task, so the driver teardown must not suspend.
    [[gnu::destructor]] void module_exit_now() {
        astia::net::unregister_link_driver_now(factory);
    }
} // namespace

astia::core::task<void> module_main() {
    const auto registered = co_await astia::net::register_link_driver(factory);

    if (!registered.has_value || !registered.value) {
        logs::error("link driver registration failed");

        co_return astia::error_t::enodev;
    }

    logs::info("ready");

    co_return astia::error_t::enone;
}
