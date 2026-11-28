# UDP module

Loadable `protocol_id::udp` handler with an asynchronous transport core.

- `headers/udp.hpp` owns the RFC 768 header, the IPv4 pseudo-header and the
  checksum helper.
- `proto/udp.cpp` validates the fixed header (length, ports, checksum), owns
  the bound-port bitmap and the socket table, and demultiplexes by destination
  port and local address.
- `socket.cpp` provides `udp::socket`:
  - `bind` and `connect` are coroutines that complete without suspending;
  - `send` awaits the IPv4 transmit path and prepends the UDP header;
  - `receive` parks on a one-shot completion until RX queues a datagram, a
    token is cancelled, or the socket is destroyed; `try_receive` is the
    non-blocking variant.
  - RX never resumes a consumer inline while holding the socket lock: waiters
    go through `core::completion`/`schedule_resume`.
- The receive queue has per-socket count/byte backpressure (32 datagrams,
  64 KiB) and a bounded waiter pool, so RX does not allocate a socket.
- `module_main()` registers the protocol; `.fini_array` unregisters it
  synchronously, so the module stays unloadable.
- `run-qemu-load.sh` builds the module, stages it as `mod/udp.km`, boots the
  module runtime and checks `mod: mod/udp.km rc=0`.

The host test (`udp_unit`) covers the checksum vector, receive demux, async
receive wake/cancel, rejected lengths/checksums, port allocation and the
transmit header.

Next: an `AF_INET` `SOCK_DGRAM` bridge in the kernel AIO layer, plus routing and
neighbour resolution for `socket::send`. The IP module already dispatches
`protocol_id::udp` to this handler.
