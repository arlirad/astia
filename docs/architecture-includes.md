# Architecture include boundaries

Architecture headers are split into two layers:

* the generic tree, `kernel/include/astia/arch/`, which is visible to all code;
* one overlay tree per port, `kernel/include/arch/x86_64/` and
  `kernel/include/arch/aarch64/`, which is placed before the generic include
  root by `kernel/CMakeLists.txt`.

The include order is overlay first, generic second.  A header with the same
`<astia/arch/...>` name in both trees therefore resolves to the selected
port's overlay; a header that only exists in the generic tree stays reachable.
Architecture-private headers also live under
`kernel/include/astia/arch/<arch>/`, and source-owned code lives under
`kernel/src/arch/<arch>/` and the `<arch>`-named boot and platform trees.

## Ownership

A file is owned by an architecture when any path component matches the
architecture name, starts with `<arch>-` or `<arch>_`, or ends with
`_<arch>`.  This is a path rule, not a filename heuristic:

* `include/arch/<arch>/**`, `include/astia/arch/<arch>/**`,
  `src/arch/<arch>/**`, `src/boot/<arch>-*/**`, `src/plat/<arch>-*/**`, and
  `src/test/selftest_<arch>.cpp` are owned by `<arch>`.
* Everything else under `include/` and `src/` is generic.

## Rules

* Generic code must not include `astia/arch/<arch>/*`, `arch/<arch>/*`, or a
  path containing `include/arch/`.
* Code owned by `<arch>` may include generic headers and its own architecture
  headers, but never the other architecture's headers.
* A file must not include itself or include the same resolved file twice.
* A relative include must not escape the kernel root.
* Every generic architecture header must compile standalone with the generic
  include root plus either overlay.  Every overlay header must compile
  standalone with its own compiler and overlay.
* The single textual exception is
  `include/astia/arch/user_layout.hpp`, which deliberately dispatches to
  `astia/arch/x86_64/user_layout.hpp` or
  `astia/arch/aarch64/user_layout.hpp`.  It is recorded with a reason in the
  test's allowlist.

AArch64 userspace is not implemented yet, so
`include/astia/arch/user_layout.hpp` and
`include/astia/arch/aarch64/user_layout.hpp` intentionally emit
`#error "Unsupported AArch64 user-layout policy..."` under the AArch64
compiler.  The compile matrix asserts that exact diagnostic instead of
pretending those two headers compile.

## Test

The test scans direct `#include` edges and compiles every header in
`include/astia/arch/**` and `include/arch/<arch>/**` with
`-std=c++20 -Wall -Wextra -Werror -pedantic -fsyntax-only`.  It also builds
textual fixtures for cross-architecture includes, self-includes, duplicate
includes, path escapes, and an allowed include; a compile fixture proves
overlay shadowing, and another proves a header that relies on a transitive
include fails standalone.

```sh
ctest --test-dir build-tests -R '^astia_arch_include_boundaries$' --output-on-failure
```

The AArch64 compiler requirement is the same one used by
`astia_target_configuration`: install `g++-aarch64-linux-gnu` or set
`ASTIA_AARCH64_VIRT_CXX_COMPILER`.  The test fails with that diagnostic rather
than skipping the AArch64 half of the matrix.

To express a new legitimate edge, add the source/target pair and reason to
`INCLUDE_ALLOWLIST` in `kernel/tests/arch_include_boundaries.py`; do not weaken
the rule.
