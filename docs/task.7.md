# Kernel coroutine tasks and fibers

## NAME

`core::task<T>`, `core::task<void>` - movable, stackless coroutine operations;
`core::fiber` - experimental stackful kernel execution context.

## SYNOPSIS

```cpp
core::task<T> operation();
core::task<void> operation_without_value();

bool task.resume();
bool task.done() const;
bool task.valid() const;
error_t task.error() const;

bool fiber.start();
bool fiber.resume();
static bool fiber::yield();
```

## DESCRIPTION

Astia uses C++20 coroutines for stackless asynchronous control flow. A
`core::task<T>` owns a coroutine handle and starts suspended. `task<T>` stores a
returned value or an `error_t`; `task<void>` is the error-only form. Both are
move-only and require one owner to decide when `resume()` is called.

Locals that must survive an await live in the coroutine frame. Awaiting a task
links its child continuation to its parent, and the child's final suspend
returns to that continuation. A task must not be resumed concurrently by more
than one CPU.

## OPERATION

`resume()` runs one step, or a waiting child, and returns false when the task is
empty or done. `co_await` consumes the child result and destroys the child
frame. `value()` exposes a completed `task<T>` value; `error()` and `failed()`
report the promise result.

`task<void>` coroutine frames use a lock-free pool of 32 slots, each limited to
2048 bytes. `task<T>` frames use the ordinary allocation path. The pool bounds
frame storage only; it does not own objects referenced by a frame.

## LIFETIME/OWNERSHIP

Destroying or resetting a task destroys its coroutine frame. Destroying a
`task<void>` returns its slot to the pool. Moving transfers the sole task
owner and clears the source. Referenced files, buffers, and other objects must
remain valid for the whole operation unless the frame stores an owning handle.

`core::fiber` is stackful. It is not compiled into the production kernel by
default. Enable `-DASTIA_BUILD_EXPERIMENTAL_FIBER=ON` to compile it into the
core library for architecture bring-up and focused experiments. Starting one
allocates a 16 KiB kernel stack and two architecture contexts. Its entry
function may call ordinary code and suspend that call stack with
`fiber::yield()`. The fiber remains owned by the CPU on which it starts.

## ERRORS

An oversized `task<void>` frame or an exhausted frame pool produces an invalid
task carrying `error_t::enomem`. Check `valid()`, `done()`, or `error()` before
resuming it; awaiting an allocation-failed task yields `error_t::enomem`.

`fiber::start()`, `fiber::resume()`, and `fiber::yield()` return false for an
invalid or completed operation, an invalid entry point, allocation or context
failure, CPU migration, or a nested active fiber.

## NOTES

Use a task when suspension points fit a coroutine frame. Use the experimental
fiber only when preserving a conventional call stack is the requirement;
`core::fiber` is not integrated with `proc::thread` scheduling. Neither
primitive provides scheduling, cancellation, or cross-CPU migration by itself.
