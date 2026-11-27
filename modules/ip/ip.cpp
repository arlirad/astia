#include <astia/core/hook.hpp>
#include <astia/core/task.hpp>
#include <astia/log.hpp>
#include <astia/module.hpp>
#include <astia/net/events.hpp>
#include <astia/net/neighbour.hpp>
#include <astia/net/protocol.hpp>
#include <astia/net/registry.hpp>

#include "config.hpp"
#include "discoverers/arp.hpp"
#include "proto/icmp.hpp"
#include "proto/ipv4.hpp"
#include "types/protocol.hpp"

namespace {
    using logs = astia::log::context<"ip">;

    constexpr const char* hook_id = "ip";

    /// Until DHCP/configuration exists, every Ethernet link gets the static
    /// QEMU user-net address.
    astia::core::hook::outcome on_link_bound(astia::net::link& value) {
        if (value.kind() == astia::net::link_kind::ethernet) {
            const astia::ip::ipv4_address local{astia::ip::default_local_address};

            (void) value.assign_address(
                astia::net::protocol_id::ipv4,
                astia::span<const astia::u8>(local.bytes(),
                                             astia::ip::ipv4_address::size));
        }

        return astia::core::hook::outcome::pass;
    }

    astia::ip::ipv4_protocol  ipv4{};
    astia::ip::icmp_protocol  icmp{};
    astia::ip::arp_protocol   arp{};
    astia::ip::arp_discoverer arp_discoverer{};

    // The async module ABI runs fini handlers synchronously inside the exit
    // task, so teardown must not suspend.
    [[gnu::destructor]] void module_exit_now() {
        astia::net::unregister_protocol(ipv4);
        astia::net::unregister_protocol(icmp);
        astia::net::unregister_protocol(arp);
        astia::net::unregister_discoverer(arp_discoverer);
        astia::net::events::link_bound::remove_quiesced(hook_id);
    }
} // namespace

// IPv4 ingress for every link that translates its medium header to the
// canonical ipv4 protocol id.  Validated datagrams go through the generic
// protocol registry; ICMP and ARP register themselves, and ARP doubles as
// the IPv4 neighbour discoverer.
astia::core::task<void> module_main() {
    if (!astia::net::register_protocol(arp)) {
        logs::error("ARP protocol registration failed");

        co_return astia::error_t::eaddrinuse;
    }

    if (!astia::net::register_protocol(icmp)) {
        astia::net::unregister_protocol(arp);

        logs::error("ICMP protocol registration failed");

        co_return astia::error_t::eaddrinuse;
    }

    if (!astia::net::register_protocol(ipv4)) {
        astia::net::unregister_protocol(arp);
        astia::net::unregister_protocol(icmp);

        logs::error("IPv4 protocol registration failed");

        co_return astia::error_t::eaddrinuse;
    }

    if (!astia::net::register_discoverer(arp_discoverer)) {
        astia::net::unregister_protocol(ipv4);
        astia::net::unregister_protocol(arp);
        astia::net::unregister_protocol(icmp);

        logs::error("ARP discoverer registration failed");

        co_return astia::error_t::enomem;
    }

    if (!astia::net::events::link_bound::add(hook_id, astia::core::hook::priority::normal,
                                             &on_link_bound)) {
        astia::net::unregister_protocol(ipv4);
        astia::net::unregister_protocol(arp);
        astia::net::unregister_protocol(icmp);
        astia::net::unregister_discoverer(arp_discoverer);

        logs::error("link hook registration failed");

        co_return astia::error_t::enomem;
    }

    // Links bound before this module loaded still need their address.
    astia::net::registry& known = astia::net::devices();

    for (astia::u32 index = 0; index < known.count(); ++index) {
        astia::net::netdev* device = known.at(index);

        if (device != nullptr && device->attached() != nullptr)
            (void) on_link_bound(*device->attached());
    }

    logs::info("ready");

    co_return astia::error_t::enone;
}
