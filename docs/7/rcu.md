# RCU and lockless constraints

## NAME

`core::rcu_domain` - bounded epoch reclamation; `rcu::vector` -
reference-counted copy-on-write snapshots; `core::rcu_hashmap` - immutable
lockless snapshots.

## SYNOPSIS

```cpp
core::rcu_domain<CpuCount, RetireCapacity> domain;
core::rcu_domain<>::read_guard guard(domain, cpu);
bool domain.retire(cpu, value, destroy);
u32 domain.poll();

rcu::vector<T, Growth = 8> vector;
rcu::vector<T, Growth>::reader snapshot(vector);
{
    rcu::vector<T, Growth>::writer writer(vector);
    bool writer.push_back(value);
    bool writer.assign(values);
    bool writer.erase(predicate);
}

core::rcu_hashmap<Key, Value> map(hash, equal);
auto value = map.find(key);
bool map.insert_or_assign(key, value);
bool map.erase(key);
```

## DESCRIPTION

Astia's RCU structures publish immutable snapshots. A reader loads one
snapshot and traverses it without a mutex. A writer constructs a replacement,
publishes it atomically, and retires the old snapshot. `rcu::vector` readers
pin their snapshot by reference count; the last reader frees a retired
snapshot, so the vector needs no grace period. `rcu_hashmap` keeps the
`core::rcu_grace` epoch machinery for reclamation.

## OPERATION/DATA FLOW

`rcu_domain<CpuCount, RetireCapacity>` tracks per-CPU reader nesting and
epochs. A `read_guard` records the current epoch on the first nested read.
`retire()` stores a value and destructor in the selected CPU's bounded retire
queue. `poll()` advances the epoch and invokes destructors only after all
readers that could have seen the retired epoch have quiesced. `quiescent()`
updates an inactive CPU's observed epoch.

`rcu::vector<T, Growth>` publishes one immutable copy-on-write array. A
`reader` takes the published pointer and retains it under a small swap
spinlock, so the pointer load and the reference increase are one step and a
writer cannot free the snapshot in between. The reader exposes `size()`,
`capacity()`, `at()`, indexed const access, and `find()`, and drops its
reference in its destructor. A `writer` holds the writer lock for its
lifetime: `push_back()` clones the snapshot and grows capacity by `Growth`
when it is full, `assign()` replaces the whole contents, and `erase()` drops
the first matching element. Publishing drops the table's reference to the old
snapshot; the last reader frees it. There is no grace period, quiescence loop,
or external reclamation pass for the vector, and the vector destructor
releases the published snapshot so readers may outlive it.

`rcu_hashmap` shards immutable bucket snapshots. `read_guard` searches one
shard, while `find()` copies the value before releasing its guard. Updates
clone the selected shard, insert or remove the key, and retry CAS publication
on contention. Bucket nodes retain their successor through owning shared
references.

`rcu_hashmap` keeps a per-shard grace epoch and reader count. A writer
publishes the replacement before retiring the old snapshot, and `retire()`
records the epoch observed at retirement. `reclaim()` advances the epoch,
detaches the retired list, and only then observes the reader count. Every
detached snapshot was unpublished before it entered that list. A reader that
enters after the observation can still load an older snapshot that is still
published, but that snapshot is not in the detached list. Any reader that
could have loaded a detached snapshot must have entered before that snapshot
was retired; if it is still active at the observation, reclamation is
deferred. Thus a snapshot that is still published when the list is detached,
or still held by a reader that entered before its retirement, cannot be freed
by that reclaim. A retirement concurrent with one or more epoch advances
records whatever counter value it observes, which may lie on either side of
the current reclaim boundary; the post-detach reader observation, not the
epoch value alone, is what prevents freeing a snapshot that a reader can
still hold. Epochs are serial numbers compared over the 2^64 ring: a record
is eligible only when its retirement epoch lies strictly in the half-range
before the reclaim boundary. A distance of exactly 2^63 is ambiguous; it is
treated as not-before, which can delay reclamation but cannot free a snapshot
that may still be observable. The algorithm assumes no record stays retired
across 2^63 grace advances. This is a container-local full-quiescence grace
period; unlike `rcu_domain` it needs no CPU identity because publication
always precedes retirement.

## LIFETIME/OWNERSHIP

An active guard or reader must not block, sleep, migrate CPUs, or call code
that may schedule. Keep it alive for every pointer dereference. Copy a value
or retain an owning reference before leaving the read-side section; a raw
pointer from a snapshot must not escape the reader.

A `rcu::vector` writer owns the newly allocated snapshot until publication and
drops the table's reference to the replaced snapshot. `reader` references are
independent, so a reader may outlive the vector object; the last reference
frees the snapshot. Do not mutate a published object. Destroying an
`rcu_hashmap` requires all readers to have quiesced; an `rcu_domain` must
outlive its guards and retired values.

## ERRORS

`rcu_domain::retire()` returns false for an invalid CPU or value, a missing
destructor, or a full bounded retire queue. `rcu::vector::push_back()`,
`assign()`, and `erase()` return false when the replacement snapshot cannot be
allocated; `erase()` also returns false when no element matches.
`rcu_hashmap` insertion, assignment, and erase return false when snapshot,
node, or retirement-record allocation fails. Readers return an empty optional
or null pointer when a key or snapshot is absent. Allocation and
bounded-queue failure are real paths; readers do not wait for space.

## NOTES

RCU read-side protection does not make surrounding state lockless. Obtain a
stable reference before crossing a subsystem boundary; do not call filesystem,
device, scheduler, wakeup, mapping, or destruction callbacks while holding an
unrelated lock; and mask interrupts before taking an IRQ-visible lock. Keep
lockless readers free of blocking and CPU migration.
