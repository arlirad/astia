# Syscall handler dispatch prototype

The x86-64 entry owns register decoding and packs `syscall_frame`; the C
boundary converts its stable public ABI number and argument registers into
the architecture-independent dispatcher. A syscall handler derives from
`sys::handler<Derived, Id>` to bind its compile-time signature to a public
number; the CRTP thunk adapts its `invoke` method to the table's plain function
pointer. A descriptor set rejects duplicate/out-of-range IDs at compile time,
while the existing runtime table still rejects duplicate registration and
returns `-ENOSYS` for unhandled numbers. The handler adapter converts ABI words
to declared argument types and widens the result to the ABI return type.

The existing `getpid` handler remains owned and registered by `proc`; the
focused tests use a side-effect-free fake handler to exercise dispatch without
booting the process subsystem. This prototype does not add an ABI syscall
number or replace any production registrations. There
is intentionally no synchronous `read` syscall in the public ABI: ring
submission is owned by `fs` and its AIO operation implementation by `io`.
If a compatibility `read` entry point is later required, its handler should
validate user arguments and translate them into an `io` request through an
owner-facing API; `sys` should not inspect or construct AIO ring internals.
