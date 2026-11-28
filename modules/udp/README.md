# UDP module

Loadable `protocol_id::udp` handler with a transport core.

- `headers/udp.hpp` owns the RFC 768 header, the IPv4 pseudo-header and the
  checksum helper.
- `proto/udp.cpp` validates the fixed header (length, ports, checksum), owns
  the bound-port bitmap and the socket table, and demultiplexes by destination
  port and local address.
- `socket.cpp` provides `udp::socket`: bind/connect, a bounded receive queue
  with backpressure, and transmit path that prepends the UDP header and hands
  the datagram to IPv4 through `net::tx`.
- `module_main()` registers the protocol; `.fini_array` unregisters it
  synchronously, so the module stays unloadable.
- `run-qemu-load.sh` builds the module, stages it as `mod/udp.km`, boots the
  module runtime and checks `mod: mod/udp.km rc=0`.

The host test (`udp_unit`) covers the checksum vector, receive demux,
rejected lengths/checksums, port allocation and the transmit header.

Next: an AF_INET `SOCK_DGRAM` bridge in the kernel AIO layer, plus routing and
neighbour resolution for `socket::send`. The IP module already dispatches
`protocol_id::udp` to this handler.
