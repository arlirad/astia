# RCU and lockless constraints

Astia's RCU containers publish immutable snapshots. Readers take a guard,
load one snapshot, and traverse it without a mutex. Writers clone or replace
the snapshot with an atomic update and retire the old object only after a grace
period. The bounded `rcu_domain` advances epochs and reclaims retired objects
from maintenance context.

## Reader rules

An active RCU reader must not block, sleep, or migrate to another CPU. Keep the
guard alive for every pointer dereference and release it before calling code
that may schedule, acquire an unrelated lock, or destroy an object. A reader
may copy a value or retain an owning reference and then leave the read-side
section.

`rcu_vector` exposes an immutable array through `read_guard`. `rcu_hashmap`
uses sharded immutable buckets and CAS publication. Both make the snapshot
stable for the guard, not forever; pointers cannot escape without a copied
value or explicit ownership.

## Writer and reclamation rules

Writers allocate the next snapshot before publication and retry a failed CAS.
They retire the replaced snapshot and report bounded allocation or retire-queue
failure instead of waiting in a reader section. `rcu_domain::poll()` may run
destructors only after all pre-existing readers have quiesced.

The lockless design does not make surrounding state lockless. Follow the
kernel lock contract:

- obtain a stable reference or snapshot before crossing a subsystem boundary;
- never call filesystem, device, scheduler, wakeup, mapping, or destruction
  callbacks while holding a lock;
- do not take an IRQ-visible lock without masking interrupts first;
- keep lockless readers free of blocking and CPU migration;
- treat bounded queues and allocation failures as real error paths.

An RCU guard is a read-side lifetime guarantee, not permission to mutate the
published object. Mutations belong in a newly allocated snapshot or under the
owning subsystem's lock.
