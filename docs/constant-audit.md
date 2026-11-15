# A-07 constant audit

The A-07 inventory distinguishes hardware-representation bounds from policy
limits and bounded queues.  The CPU topology bound is supplied by
`astia::arch::platform`; policy and queue limits remain local to the subsystem
that owns them.

| Subsystem | Constant | Classification | Ownership and behavior |
| --- | --- | --- | --- |
| Architecture/platform | `arch::platform::max_cpu_count` (256) | `platform_max` | Single bound for per-CPU arrays in the scheduler, SMP state, GDT/TSS state, process current-thread state, AIO recursion state, and resume queues. |
| Scheduler | `scheduler::max_cpu_count` | compatibility alias of `platform_max` | Preserves the existing scheduler-facing name without another storage bound. |
| Scheduler | `run_queue_capacity` (32) | `queue_capacity` | Per-CPU runnable queue; full queues still reject enqueue and migration. |
| SMP/GDT | APIC, reschedule, GDT, TSS, and CPU-local arrays | `platform_max` | Use `arch::platform::max_cpu_count`; no AP startup or allocation policy was widened. |
| AIO | `ASTIA_AIO_MAX_RING_SLOTS` (4), `ASTIA_AIO_MAX_RING_ENTRIES` (64), `ASTIA_AIO_MAX_OPERATION_BYTES` | `policy_max` | Public read-only contract for the static ring-pool, ring-entry, and operation-size limits; setup rejects entries above the cap with `-EINVAL`, and a fifth ring returns `-ENFILE`. |
| AIO | `ASTIA_AIO_MAX_PARKED_OPERATIONS` (64), waiter buckets | `queue_capacity` | Public read-only per-ring parked-operation queue and waiter-index storage; an operation beyond the full queue completes with `-EBUSY`. |
| AIO | `ring_lock::cpu_slots` | `platform_max` | Per-CPU recursion bookkeeping uses the platform bound. |
| Resume queue | `queue_capacity` (256) | `queue_capacity` | Two bounded MPMC queues and their slot pool per CPU; intentionally independent of CPU topology. |
| Process/thread | `max_processes` (64), `max_threads_per_process` (16), waiter/group/argv limits | `policy_max` | Resource and ABI safety limits; unchanged. The current-thread array uses `platform_max`. |
| PCI | `pci_registry::max_devices` (256), resource table (256), BAR/capability array bounds | `policy_max` | Enumeration, resource, and protocol safety limits; intentionally not tied to CPU topology and unchanged. |

Boundary coverage is provided by `astia_platform_test`: zero, one, the exact
platform maximum, maximum plus one, and the largest `u64` are checked.  The
existing MPMC and task tests cover exact queue fill and overflow/lossless
deferred completion (`astia_mpmc_queue_test` and `astia_task_test`).

No process, thread, PCI, AIO ring, scheduler queue, resume queue, or operation
safety limit was increased or merged into the platform provider.
