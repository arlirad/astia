# Syscall handler dispatch prototype

The x86-64 entry owns register decoding and packs `syscall_frame`; the C
boundary converts its stable public ABI number and argument registers into the
architecture-independent dispatcher. A syscall handler derives from
`sys::handler<Derived, Id>` to bind its compile-time signature to a public
number; the CRTP thunk adapts its `invoke` method to the table's plain function
pointer. A descriptor set rejects duplicate/out-of-range IDs at compile time,
while the existing runtime table still rejects duplicate registration and
returns `-ENOSYS` for unhandled numbers. The handler adapter converts ABI words
to declared argument types and widens the result to the ABI return type.

The isolated `kernel/tests/syscall_prototype/` tree contains one handler file
for every public ABI syscall number. Files are grouped under their owning
subsystem (`fs`, `mm`, `ipc`, `proc`, or `sys`) and each CRTP handler exposes a
completion object with exactly-once completion and cancellation. The
prototype registry only aggregates descriptors; it contains no syscall
semantics. Its test verifies every public ID is registered and dispatched, and
checks pending, complete, and cancel behavior.

These handlers are contract and layout examples; they return `-EINPROGRESS`
and do not replace or modify production syscall registrations or behavior.
Socket operation prototypes live under `net/syscalls/`, with private
prototype-only IDs 70–81. They model the socket AIO opcodes; they do not add
public syscall numbers or change the public ABI. Socket semantics belong to
`net`; `fs` owns AIO ring submission and delegates I/O execution through the
existing `io` boundary. There is intentionally no synchronous `read` syscall
in the public ABI. If a compatibility `read` entry point is later required,
its handler should validate user arguments and translate them into an `io`
request through an owner-facing API; `sys` should not inspect or construct
AIO ring internals.
