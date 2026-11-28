#include "proto/udp.hpp"

#include <astia/core/task.hpp>
#include <astia/log.hpp>
#include <astia/module.hpp>
#include <astia/net/protocol.hpp>

namespace {
    using logs = astia::log::context<"udp">;

    astia::udp::udp_protocol protocol{};

    // The async module ABI runs fini handlers synchronously inside the exit
    // task, so teardown must not suspend.
    [[gnu::destructor]] void module_exit_now() {
        astia::net::unregister_protocol(protocol);
    }
} // namespace

astia::core::task<void> module_main() {
    if (!astia::net::register_protocol(protocol)) {
        logs::error("UDP protocol registration failed");

        co_return astia::error_t::eaddrinuse;
    }

    logs::info("ready");

    co_return astia::error_t::enone;
}
