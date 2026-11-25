# N-11 fixed-resource contract audit

This inventory records fixed bounds that can reject an otherwise valid
operation.  A limit is **ABI** when changing it changes a public layout or
published value contract, **platform** when it describes hardware topology or
an architecture representation, **policy** when it is a kernel resource
choice, **queue** when it bounds pending work rather than live objects, and
**implementation** when it is an internal dispatch or bookkeeping table.
The names in `astia/resource_limits.hpp` are the C++ source of truth for the
centralized limits.  The AIO C ABI constants remain in `astia/api/aio.h` and
are checked against that header at compile time.

| Area | Name | Value | Class | Full-boundary result |
| --- | --- | ---: | --- | --- |
| CPU topology | `arch::platform::max_cpu_count` | 256 | platform | CPU counts clamp at 256; per-CPU arrays never index beyond it. |
| AIO ring pool | `ASTIA_AIO_MAX_RING_SLOTS` / `queue::aio_ring_slots` | 16 | ABI / queue | Seventeenth live ring returns `-ENFILE`. |
| AIO SQ | `ASTIA_AIO_MAX_RING_ENTRIES` / `queue::aio_ring_entries` | 64 | ABI / policy | Entries above 64 return `-EINVAL`; 64 is accepted. |
| AIO parked operations | `ASTIA_AIO_MAX_PARKED_OPERATIONS` / `queue::aio_parked_operations` | 64 per ring | ABI / queue | A 65th park completes with `-EBUSY`; 64 is retained. |
| AIO operation bytes | `ASTIA_AIO_MAX_OPERATION_BYTES` | `0x7ffff000` | ABI / policy | Larger requests return `-EINVAL`. |
| Process registry | `policy::process_count` / `proc::max_processes` | 64 slots | policy | Registry insertion fails; process creation reports `-EAGAIN`. |
| Threads | `policy::threads_per_process` / `proc::max_threads_per_process` | 16 | policy | The 17th thread creation reports `-EAGAIN`. |
| Process descriptors | `policy::process_file_descriptors` / `proc::max_process_fds` | 64 | ABI / policy | No descriptor is allocated at index 64; descriptor operations report the existing fd error. |
| Process waiters | `policy::process_waiters` | 4 | queue | Additional process waiters are rejected without replacing an existing waiter. |
| Signal waiters | `queue::signal_waiters` | 16 | queue | The 17th waiter is rejected; wake snapshots are bounded by 16. |
| Supplementary groups | `policy::supplementary_groups` | 16 | ABI / policy | `setgroups(17, ...)` returns `-EINVAL`. |
| Spawn vectors | `policy::argv_entries`, `policy::env_entries` | 16 each | ABI / policy | Excess startup entries are rejected rather than silently truncated. |
| Spawn strings | `policy::spawn_string_bytes` | 2048 | policy | An oversized startup string is rejected with the existing validation error. |
| I/O vectors | `policy::iovec_segments` / `mm::iovec::max_segments` | 8 | ABI / policy | A ninth `readv`/`writev` entry returns `-EINVAL`; entries are never truncated. |
| PCI registry | `policy::pci_devices` / `pci_registry::max_devices` | 256 | policy | The 257th device is not inserted; enumeration stops at the cap. |
| PCI resources | `policy::pci_resources` | 256 | policy | The 257th BAR reservation fails without overwriting an owner. |
| PCI BAR array | `abi::pci_bar_count` | 6 | ABI / platform | BAR indices 0–5 are representable; index 6 is rejected. |
| PCI capability array | `abi::pci_capability_count` | 48 | ABI / platform | Capability traversal stops at 48 entries and rejects malformed loops. |
| PCI extended capabilities | `abi::pci_extended_capability_count` | 32 | ABI / platform | Extended traversal stops at 32 entries and rejects malformed loops. |
| Driver manager | `policy::registered_drivers`, `policy::bound_devices` | 32, 64 | policy | Registration/binding fails at the exact full count. |
| Init ordering | `policy::init_steps` | 128 | policy | 128 steps are accepted; 129 returns failure before execution. |
| Syscall dispatch | `abi::syscall_table_capacity` | 128 | ABI / implementation | IDs 0–127 are table-addressable; an unregistered or larger ID returns `-ENOSYS`. |
| Coroutine frames | `coroutine::frame_slot_bytes`, `coroutine::frame_slots` | 2048 bytes, 32 | policy / queue | A 2048-byte frame fits; a 2049-byte frame or 33rd live frame returns `ENOMEM`. |
| Scheduler runnable queue | `queue::scheduler_runnable` | 32 per CPU | queue | Enqueue at 32 is rejected with the existing wake failure; no entry is lost. |
| Resume queues | `queue::resume` | 256 per queue | queue | The implementation's overflow queue preserves deferred resumes up to its existing slot pool. |
| RCU retired queue | `queue::rcu_retired` | 256 per CPU | queue | The 257th retirement returns false; callers retain ownership on failure. |
| Futex waiters | `queue::futex_waiters`, `queue::futex_buckets` | 64, 4 | queue | The bounded waiter table rejects the next waiter without overwriting a bucket. |
| Unix datagrams | `queue::unix_datagrams` | 32 | queue | The 33rd pending datagram returns the existing queue-full error. |

Resource exhaustion uses the existing public errno contract: `-ENFILE` for
the finite AIO ring pool, `-EBUSY` for a full per-ring parked-operation queue,
`-EAGAIN` for process/thread policy exhaustion, `-ENOMEM` for allocation or
coroutine-frame exhaustion, and `-ENOSYS` for an unavailable syscall slot.
Boolean and internal queue APIs preserve their existing full/false result so
callers can decide whether to retry, block, or report the corresponding ABI
error.  This audit does not change any capacity or introduce dynamic storage.
