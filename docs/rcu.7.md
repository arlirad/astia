# RCU and lockless constraints

## NAME

`core::rcu_domain` - bounded epoch reclamation; `core::rcu_vector` and
`core::rcu_hashmap` - immutable lockless snapshots.

## SYNOPSIS

```cpp
core::rcu_domain<CpuCount, RetireCapacity> domain;
core::rcu_domain<>::read_guard guard(domain, cpu);
bool domain.retire(cpu, value, destroy);
u32 domain.poll();

core::rcu_vector<T, Capacity> vector;
core::rcu_vector<T, Capacity>::read_guard snapshot(vector);
bool vector.replace(values);

core::rcu_hashmap<Key, Value> map(hash, equal);
auto value = map.find(key);
bool map.insert_or_assign(key, value);
bool map.erase(key);
```

## DESCRIPTION

Astia's RCU structures publish immutable snapshots. A reader acquires a guard,
loads one snapshot, and traverses it without a mutex. A writer constructs a
replacement, publishes it atomically, and retires the old snapshot. A guard
protects the snapshot only for its read-side lifetime.

## OPERATION/DATA FLOW

`rcu_domain<CpuCount, RetireCapacity>` tracks per-CPU reader nesting and
epochs. A `read_guard` records the current epoch on the first nested read.
`retire()` stores a value and destructor in the selected CPU's bounded retire
queue. `poll()` advances the epoch and invokes destructors only after all
readers that could have seen the retired epoch have quiesced. `quiescent()`
updates an inactive CPU's observed epoch.

`rcu_vector<T, Capacity>` publishes one immutable bounded array. Its
`read_guard` exposes `data()`, `size()`, and indexed const access. `replace()`
allocates and fills a new snapshot, then retries a compare-and-swap publication
if another writer won. Replaced snapshots are reclaimed after a grace period.

`rcu_hashmap` shards immutable bucket snapshots. `read_guard` searches one
shard, while `find()` copies the value before releasing its guard. Updates
clone the selected shard, insert or remove the key, and retry CAS publication
on contention. Bucket nodes retain their successor through owning shared
references.

Both containers keep a per-shard or per-container grace epoch and reader
count. A writer publishes the replacement before retiring the old snapshot,
and `retire()` records the epoch observed at retirement. `reclaim()` advances
the epoch, detaches the retired list, and only then observes the reader count.
Every detached snapshot was unpublished before it entered that list. A reader
that enters after the observation can still load an older snapshot that is
still published, but that snapshot is not in the detached list. Any reader
that could have loaded a detached snapshot must have entered before that
snapshot was retired; if it is still active at the observation, reclamation is
deferred. Thus a snapshot that is still published when the list is detached,
or still held by a reader that entered before its retirement, cannot be freed
by that reclaim. A retirement concurrent with one or more epoch advances
records whatever counter value it observes, which may lie on either side of the
current reclaim boundary; the post-detach reader observation, not the epoch
value alone, is what prevents freeing a snapshot that a reader can still hold.
Epochs are serial numbers compared over the 2^64 ring: a record is eligible
only when its retirement epoch lies strictly in the half-range before the
reclaim boundary. A distance of exactly 2^63 is ambiguous; it is treated as
not-before, which can delay reclamation but cannot free a snapshot that may
still be observable. The algorithm assumes no record stays retired across 2^63
grace advances. This is a container-local
full-quiescence grace period; unlike `rcu_domain` it needs no CPU identity
because publication always precedes retirement.

## LIFETIME/OWNERSHIP

An active guard must not block, sleep, migrate CPUs, or call code that may
schedule. Keep it alive for every pointer dereference. Copy a value or retain
an owning reference before leaving the read-side section; a raw pointer from a
snapshot must not escape the guard.

Writers own the newly allocated snapshot until publication and retire the
replaced snapshot. Do not mutate a published object. Destroying an
`rcu_vector` or `rcu_hashmap` requires all readers to have quiesced; an
`rcu_domain` must outlive its guards and retired values.

## ERRORS

`rcu_domain::retire()` returns false for an invalid CPU or value, a missing
destructor, or a full bounded retire queue. `rcu_vector::replace()` returns
false when the input exceeds capacity or required allocation fails.

Hashmap insertion, assignment, and erase return false when snapshot, node, or
retirement-record allocation fails. Readers return an empty optional or null
pointer when a key or snapshot is absent. Allocation and bounded-queue failure
are real paths; readers do not wait for space.

## NOTES

RCU read-side protection does not make surrounding state lockless. Obtain a
stable reference before crossing a subsystem boundary; do not call filesystem,
device, scheduler, wakeup, mapping, or destruction callbacks while holding an
unrelated lock; and mask interrupts before taking an IRQ-visible lock. Keep
lockless readers free of blocking and CPU migration.
