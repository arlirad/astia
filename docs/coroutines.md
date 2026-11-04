# Kernel coroutines and fibers

Astia uses C++20 coroutines for stackless asynchronous control flow. A
`core::task<T>` owns a coroutine handle and starts suspended. The caller owns
when to call `resume()`, and a task must not be resumed concurrently from more
than one CPU. Awaiting a task links the child continuation to its parent; the
child's final suspend returns to that continuation.

## `task<T>` and `task<void>`

`task<T>` stores a returned value or an `error_t` in its promise. Its frame uses
the ordinary allocation path. `task<void>` is the error-only form used by
operations that do not return a value, and has the same suspended, movable,
single-owner lifetime model.

Both forms are stackless: local variables that must survive an await live in
the coroutine frame, not on a private execution stack.

## The `task<void>` frame pool

`task<void>` allocates frames from a lock-free fixed pool of 32 slots, each
limited to 2048 bytes. A frame larger than one slot or an exhausted pool cannot
be allocated. Coroutine allocation failure is converted to a task carrying
`error_t::enomem`; callers must check `valid()`, `done()`, or `error()` before
assuming that a task can run. Destroying a task returns its pool slot.

```cpp
auto operation = start_operation();
if (!operation.valid())
    return operation.error();
operation.resume();
```

The pool bounds frame storage, not the memory held by objects referenced from a
frame. Those objects still need explicit ownership and lifetime management.

## Stackful fibers

`core::fiber` is a different primitive. Starting a fiber allocates a separate
16 KiB kernel stack and two architecture contexts. Its entry function can call
ordinary code and yield from that stack with `fiber::yield()`. A fiber is
owned by the CPU on which it starts; `resume()` and `yield()` reject migration
and nested active fibers. Use a fiber when preserving a call stack is the
requirement, and a task when suspension points can be represented by a
coroutine frame.
