# Ethernet layer module

Loadable Ethernet II link layer. `module_main()` registers an
`astia::eth::driver` with `net::link_bus`; the bus binds it to every Ethernet
`netdev` it matches, and the driver owns up to four links. Success is
`astia::error_t::enone`.

Each link does the framing: minimum-size padding and EtherType insertion on
transmit, destination filtering and EtherType demux on receive, and translation
between EtherType and the canonical `net::protocol_id` via `mapping.hpp`. Wire
headers live in `headers/` and the field types they use in `types/`.

Teardown runs through the validated `.fini_array` path. The module registers a
`[[gnu::destructor]]` that unregisters the driver synchronously, because the
module exit task cannot suspend, and there is no explicit `module_exit()`
symbol.

IPv4 and ARP resolution are not implemented here; the module only carries the
Ethernet wire layout for them.
