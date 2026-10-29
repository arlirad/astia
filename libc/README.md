# Astia libc process helpers

The process interfaces in `include/unistd.h` and `include/sys/wait.h` are
thin wrappers over Astia's native process ABI.  `execl`, `execle`, and
`execlp` construct the `argv` vector required by `execve`; they only return
`-1` when the native image replacement fails.  `wait` is the `waitpid(-1,
status, 0)` convenience form.  No host filesystem or process emulation is
used by the Astia build.
