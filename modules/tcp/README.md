# TCP module

Loadable `protocol_id::tcp` implementation on the async IPv4 path.

- `headers/tcp.hpp` owns the RFC 9293 header, flag helpers, the IPv4
  pseudo-header and the checksum (via `net::acc_checksum`).
- `proto/tcp.cpp` keeps a fixed TCB table plus listeners and implements:
  - passive/active open (SYN, SYN-ACK, ACK),
  - in-order receive ring with out-of-order reassembly,
  - peer window tracking and blocking async send,
  - retransmission queue with a 1 s RTO and exponential backoff,
  - graceful FIN_WAIT_1/2, CLOSE_WAIT, LAST_ACK and TIME_WAIT,
  - RST for unknown segments and hard abort.
- MSS is negotiated in SYN/SYN-ACK and caps outgoing segments.

`connection` exposes the async-native surface:

```cpp
core::task<i64>         send(span<const u8> data, core::cltoken token = {});
core::task<i64>         receive(span<u8> data, core::cltoken token = {});
core::task<connection*> accept(core::cltoken token = {});
core::task<i64>         wait_established(core::cltoken token = {});
```

`tcp_protocol::try_send`/`try_receive` are the non-blocking helpers.

`module_main()` registers the protocol; `.fini_array` unregisters it
synchronously, so the module stays unloadable. `run-qemu-load.sh` stages
`mod/tcp.km` and checks `mod: mod/tcp.km rc=0`.

Host tests drive synthetic segments through the protocol, inspect the
captured IPv4 sends and advance the fake timer for retransmission/backoff.
