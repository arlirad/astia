# IP stack module

Loadable IPv4 layer built on the async link bus.

- `module_main()` registers the canonical `protocol_id::ipv4` handler;
  teardown removes it synchronously through the validated `.fini_array` path,
  so the module stays unloadable.
- `proto/ipv4.cpp` validates incoming datagrams (version, IHL, total length,
  header checksum, local destination) and dispatches by IP protocol.
- `proto/icmp.cpp` answers ICMP echo requests with echo replies, preserving
  identifier, sequence, and payload.
- `headers/` holds the wire structs; `types/` the typed fields (addresses,
  ports, protocol/ICMP numbers).

The local address is the static `10.0.2.15` from `config.hpp` until DHCP or
configuration lands. ARP resolution (requests, replies, and neighbour
learning) lives in `discoverers/arp.cpp`; echo replies go straight to the
request's link-layer source.
