# Astia libc process helpers

The process interfaces in `include/unistd.h` and `include/sys/wait.h` are
thin wrappers over Astia's native process ABI.  `execl`, `execle`, and
`execlp` construct the `argv` vector required by `execve`; they only return
`-1` when the native image replacement fails.  `wait` is the `waitpid(-1,
status, 0)` convenience form.  No host filesystem or process emulation is
used by the Astia build.

## Feature-test macros

Astia's freestanding libc deliberately has no glibc-style feature-test
layer: there is no `<features.h>` and no `__USE_*` derivation.  The
request macros `_POSIX_C_SOURCE`, `_XOPEN_SOURCE`, `_GNU_SOURCE`, and
`_DEFAULT_SOURCE` are accepted but inert; they never gate a declaration
or hide an implementation.  Every implemented header and API is
unconditional, and that set is the documented API superset.  Requests for
unimplemented APIs remain missing at the symbol level rather than being
faked behind a preprocessor branch.

The `libc_feature_macros` test compiles every installed header standalone
with strict C11 diagnostics in the default mode and with each request
macro individually and in combination.  It also verifies that request
macros remain visible, that no derived `__USE_*`, `__GLIBC__`,
`__header_inline`, or `__UBCLIBC__` macros appear, and that
unconditional APIs such as `strdup` and `fileno` still compile.
