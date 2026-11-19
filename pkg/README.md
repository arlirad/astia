<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Arlirad -->

# Astia package manager prototype

`pkg` is a host-Linux prototype for the future Astia package manager. It is
deliberately sandboxed: the default root is `build/tmp/pkg-root`, and a root of
`/` or `/usr` is rejected. Tests create temporary roots and repositories and
never modify a host system.

## Commands and target CPU architecture

The compact interface follows a deliberately small pacman-like grammar:

* `-s` (or `sync`) validates and atomically caches `index.json`; it requires
  `--repo`.
* `-i NAME [VERSION]` (or `install`) installs the selected version from the
  cached index.
* `-r NAME` (or `remove`) removes one installed package.
* `-is NAME [VERSION]` syncs, then installs, in that order.
* `-su` syncs, then upgrades every package already installed to the newest
  compatible version. Each package is a separate transactional upgrade; a
  later failure does not undo an earlier package transaction.

Only `-is` and `-su` are accepted as combined flags. Other combinations,
extra operands, and missing `--repo` fail before any package operation. The
long `list` and `status` commands remain available.

The target is a CPU architecture, not a generic ABI: `x86_64`, `aarch64`,
and `riscv64` are examples. The prototype detects the host CPU through
`platform.machine()` (or the `ASTIA_MACHINE_ARCH` integration hook), but
`--arch ARCH` is required for cross-architecture sandbox tests and fixes the
target architecture for that root. It refuses to silently fall back to
another artifact. ABI/libc/OS variants are intentionally future metadata
dimensions, not architecture fallbacks.

```sh
./pkg/pkg --root build/tmp/pkg-root -s --repo ./repository
./pkg/pkg --root build/tmp/pkg-root -i hello [1.2.3]
./pkg/pkg --root build/tmp/pkg-root -is hello --repo ./repository
./pkg/pkg --root build/tmp/pkg-root -su --repo ./repository
./pkg/pkg --root build/tmp/pkg-root list
./pkg/pkg --root build/tmp/pkg-root status
./pkg/pkg --root build/tmp/pkg-root -r hello
```

`--repo` accepts either a local directory containing `index.json` or a static
HTTP(S) repository URL. A URL ending in `index.json` is used directly;
otherwise `/index.json` is appended. `sync` validates and atomically replaces
the cached index. Package archives are fetched only by `install`, then checked
for both indexed size and SHA-256.

## Static repository format

The repository is static JSON plus immutable gzip-compressed tar archives:

```json
{
  "schema": 1,
  "generated_at": "2026-01-01T00:00:00Z",
  "repository": {
    "name": "example",
    "channel": "stable",
    "architectures": ["x86_64", "aarch64"]
  },
  "packages": {
    "hello": {
      "versions": {
        "1.0.0": {
          "artifacts": {
            "x86_64": {
              "archive": "packages/hello-1.0.0-x86_64.tar.gz",
              "sha256": "64 lowercase hexadecimal characters",
              "size": 1234
            },
            "aarch64": {
              "archive": "packages/hello-1.0.0-aarch64.tar.gz",
              "sha256": "64 lowercase hexadecimal characters",
              "size": 1256
            }
          },
          "dependencies": []
        }
      }
    }
  }
}
```

Versions use `major.minor.patch` with an optional SemVer-style prerelease.
Each archive contains a root `package.json`:

```json
{
  "format_schema": 1,
  "name": "hello",
  "version": "1.0.0",
  "files": [
    {"path": "bin/hello", "sha256": "64 lowercase hexadecimal characters", "mode": 493}
  ]
}
```

Only regular files and directories are accepted. Paths are relative,
normalized POSIX paths; `..`, absolute paths, symlinks, hard links, devices,
duplicate members, and `.pkg` paths are rejected. Package scripts are never
executed. Dependencies are validated as metadata but installation is deferred
when a non-empty dependency list is encountered.

## Trust and transactions

This MVP uses a local unsigned index and package digests. HTTPS protects
transport, not package authenticity, and is not a substitute for signatures.
An existing `name@version` cannot silently change archive URL, size, or
digest in a root's cache. Future work should add signed index/package
metadata, preferably an Ed25519/TUF-like root of trust with expiry,
rollback/freeze protection, key rotation, and delegation. The current
transport boundary is intentionally small so Astia's native HTTP stack can
replace Python's host `urllib` implementation.

Installation extracts into a transaction staging directory, checks every
manifest and target, and commits ownership state only after all file moves
succeed. Files owned by another package or unmanaged files are never
clobbered. Remove and upgrades use the same backup-and-rollback behavior.
Ownership and installed-package state live under `.pkg`; package paths cannot
use that reserved namespace.

The prototype is not yet an Astia-native utility. Its intended next
integration step is to reuse the format, verifier, ownership model, and
transaction protocol behind Astia's filesystem, process, and HTTP APIs.
