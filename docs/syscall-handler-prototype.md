# Syscall handler dispatch

The x86-64 entry packs only the return-side register image (`arch::syscall_return`);
the syscall number and its six argument registers travel as ordinary C
parameters into the architecture-independent dispatcher. The C door maps the raw
number through `mapper<sys::syscall_id>` before dispatching. A handler derives
from the mode-last template `sys::syscall_handler<Impl, syscall_id,
syscall_mode>`. `sync` implementations return `i64`; `async` implementations
return `core::task<i64>`. Separate CRTP specializations enforce these contracts
at compile time, so dispatch contains no runtime mode branch. The production
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
    static i64 invoke();
};

struct lseek_example
    : sys::syscall_handler<lseek_example, sys::syscall_id::lseek,
                           sys::syscall_mode::sync> {
    static i64 invoke(i64 fd, i64 offset, i64 whence);
};

struct io_setup_example
    : sys::syscall_handler<io_setup_example, sys::syscall_id::io_setup,
                           sys::syscall_mode::sync> {
    static i64 invoke(u32 entries, u32 flags,
                      sys::user_ptr<astia_rings*> out);
};
```

Handlers take syscall arguments as direct typed parameters in ABI order; the
generic dispatcher performs the register-to-parameter conversion from six `u64`
registers, and the handler's parameter type decides signedness. The CRTP
handler trait centrally enforces the six-register ABI limit at compile time.
Pointer, array, and string ABI arguments use `sys::user_ptr<T>` so handlers
never reinterpret raw registers; handlers perform their own `validate()` and
copy-in/copy-out with their original null and `-EFAULT` semantics. The three
handlers that need the trap itself (`fork`, `execve`, and `sigreturn`) lead with
`sys::syscall_trap`, which borrows the six registers and the return image; that
parameter is not an ABI argument and consumes no register. The syscall number
and arguments are never stored in the return image.

`kernel/src/sys/handler_registry.hpp` aggregates all 71 public descriptors
and 12 private socket descriptors. Public IDs are 0–69 plus the system
identity descriptor 82; the private socket prototypes keep 70–81 without
entering the public ABI. The registry stores `constexpr` pointers to
the per-subsystem descriptor objects rather than value copies, so the table is
statically initialized in the freestanding kernel, which runs no dynamic
initializers. Handlers call their owning implementation directly; syscall
dispatch does not use subsystem registration tables. Tests audit complete ID
coverage and exercise the sync/async adapters, completion semantics, and
unknown-ID handling.

Synchronous syscall logic lives in its canonical handler source. Reused
subsystem operations are called directly, and syscall-only wrappers have been
removed. The generic `invoke_sync` adapter was removed with its last call
sites. The AIO syscall handlers in `kernel/src/fs/syscalls/` validate user
pointers where required or delegate to the existing `sys::io_*` boundary;
`io_register` returns `-ENOSYS`. No AIO behavior, registry, lifecycle, or
implementation was changed.

Socket operation handlers live under `kernel/src/net/syscalls/`, with private
prototype-only descriptors 70–81. They model socket AIO opcodes; they do not
add public syscall numbers or change the public ABI. Socket semantics belong
to `net`; `fs` owns AIO ring submission and delegates I/O execution through
the existing `io` boundary. There is intentionally no synchronous `read` or
public `write` syscall. A future compatibility read entry point must validate
user arguments and translate them into an `io` request through an owner-facing
API; `sys` must not inspect or construct AIO ring internals.

The syscall entry remains the C ABI boundary: `syscall.S` calls
`astia_syscall_dispatch` with the call number, the six argument registers, and a
pointer to the return image. The C door maps the number through
`mapper<sys::syscall_id>` and preserves the registers for signal delivery; the
return image carries only the caller's return state (return mode,
caller-saved image, RIP/RFLAGS/RSP, callee-saved image). The production C++
dispatcher selects each descriptor from one central registry. Each descriptor
directly invokes the implementation owned by its subsystem; subsystem
registration tables are not part of syscall dispatch.

An async handler is a `core::task<i64>` contract with the same typed arguments
as a sync handler. Long-running I/O remains on the AIO ring and its `io`
implementation.
