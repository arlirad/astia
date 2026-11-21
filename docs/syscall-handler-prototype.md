# Syscall handler dispatch

The x86-64 entry owns register decoding and packs `syscall_frame`; the C
boundary converts its stable public ABI number and argument registers into the
architecture-independent dispatcher. A handler derives from the mode-last
template `sys::syscall_handler<Impl, syscall_id, syscall_mode>`. `sync`
implementations return `i64`; `async` implementations return
`core::task<i64>`. Separate CRTP specializations enforce these contracts at
compile time, so dispatch contains no runtime mode branch. The production
handlers use sync mode: their syscall entry points return `i64`, including
operations that wait through the established syscall-continuation mechanism.
No production task wrapper is labeled async unless its handler actually
implements the task contract. Descriptor sets
reject duplicate and out-of-range IDs at compile time; unhandled numbers
return `-ENOSYS`.

The production `kernel/src/` tree contains one handler file for every public
ABI syscall number. Files are grouped under their owning subsystem (`fs`, `mm`,
`ipc`, `proc`, or `sys`) and use explicit snake_case declarations, for example:

```cpp
struct getpid
    : sys::syscall_handler<getpid, sys::syscall_id::getpid,
                           sys::syscall_mode::sync> {
    static i64 invoke(sys::syscall_context& context);
};
```

`kernel/src/sys/handler_registry.hpp` aggregates all 70 public descriptors
and 12 private socket descriptors. Handlers call their owning implementation
directly; syscall dispatch does not use subsystem registration tables. Tests
audit complete ID coverage and exercise the sync/async adapters, completion
semantics, and unknown-ID handling.

Synchronous syscall logic lives in its canonical handler source. Reused
subsystem operations are called directly, and syscall-only wrappers have been
removed. The only production `invoke_sync` sites left are the four AIO syscall
adapters, which are unchanged by this non-AIO refactor:

- `kernel/src/fs/syscalls/io_destroy.cpp`
- `kernel/src/fs/syscalls/io_register.cpp`
- `kernel/src/fs/syscalls/io_setup.cpp`
- `kernel/src/fs/syscalls/io_submit.cpp`

They retain the generic argument-to-implementation adapter at the existing
AIO boundary; no AIO behavior or implementation was changed.

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
syscall number and six argument registers. The production C++ dispatcher selects each descriptor from one central registry.
Each descriptor directly invokes the implementation owned by its subsystem;
subsystem registration tables are not part of syscall dispatch.

An async handler is a `core::task<i64>` contract. The public syscall ID remains
distinct from the continuation's internal operation identity and user data.
Long-running I/O remains on the AIO ring and its `io` implementation.
