# TCP module scaffold

Loadable `protocol_id::tcp` handler.

- `proto/tcp.cpp` validates the fixed header and drops the datagram.
- `headers/tcp.hpp` owns the wire struct; `net::port` is shared from the kernel.
- `module_main()` registers the protocol; the `.fini_array` path unregisters it
  synchronously, so the module stays unloadable.
- `run-qemu-load.sh` builds the module, stages it as `mod/tcp.km`, boots the
  module runtime and checks `mod: mod/tcp.km rc=0`.

Next: socket demultiplexing (UDP) and connection state/handshake
(TCP). The IP module already dispatches `protocol_id::udp`/`tcp` to any
registered handler.
