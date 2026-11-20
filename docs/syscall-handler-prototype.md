# Syscall handler dispatch

The x86-64 entry owns register decoding and packs `syscall_frame`; the C
boundary converts its stable public ABI number and argument registers into the
architecture-independent dispatcher. A handler derives from the mode-last
template `sys::syscall_handler<Impl, syscall_id, syscall_mode>`. `sync`
implementations return `i64`; `async` implementations return
`core::task<i64>`. Separate CRTP specializations enforce these contracts at
compile time, so dispatch contains no runtime mode branch. Descriptor sets
reject duplicate and out-of-range IDs at compile time, while the existing
runtime table still rejects duplicate registration and returns `-ENOSYS` for
unhandled numbers.

The production `kernel/src/` tree contains one handler file for every public
ABI syscall number. Files are grouped under their owning subsystem (`fs`, `mm`,
`ipc`, `proc`, or `sys`) and use explicit snake_case declarations, for example:

```cpp
struct getpid_syscall
    : sys::syscall_handler<getpid_syscall, sys::syscall_id::getpid,
                           sys::syscall_mode::async> {
    static core::task<i64> invoke(sys::syscall_context& context);
};
```

`kernel/src/sys/handler_registry.hpp` enumerates all 70 public handlers.
The dispatcher owns completion and cancellation exactly once, while existing
subsystem implementations remain the behavior behind the adapter. The tests
enumerate the complete registry and cover pending, complete, cancel,
duplicate, range, unknown-ID, and ABI-preservation behavior.

Socket operation handlers live under `kernel/src/net/syscalls/`, with private
prototype-only descriptors 70–81. They model socket AIO opcodes; they do not
add public syscall numbers or change the public ABI. Socket semantics belong
to `net`; `fs` owns AIO ring submission and delegates I/O execution through
the existing `io` boundary. There is intentionally no synchronous `read` or
public `write` syscall. A future compatibility read entry point must validate
user arguments and translate them into an `io` request through an owner-facing
API; `sys` must not inspect or construct AIO ring internals.

The register-frame entry remains the C ABI boundary: `syscall.S` calls
`astia_syscall_dispatch`, which decodes the frame and preserves the public
syscall number and six argument registers. The production C++ dispatcher first
uses the CRTP registry, then retains `legacy_dispatch` as a compatibility shim
for the existing subsystem registration tables. This shim is required because
the current handlers delegate to those tables; removing it would recurse or
drop existing behavior. It can be removed only after each subsystem's
implementation is moved behind its owning handler.

An async handler is a `core::task<i64>` contract. The adapter resumes it once
without blocking; a task that remains pending is canceled exactly once because
the synchronous public syscall ABI has no continuation slot. Long-running I/O
continues to belong to the AIO ring and its `io` implementation, rather than
blocking or fabricating an immediate syscall completion.
