# Userspace tools

Astia-specific module-management commands:

- [`modopen`](modopen.md) loads and publishes a kernel module image.
- [`modls`](modls.md) lists the published module snapshot.
- [`modclose`](modclose.md) quiesces and unloads a module.
- [`modstat`](modstat.md) prints one module snapshot.

The portable native command set is documented in
[`coreutils`](coreutils.md).

The commands are built by `utils/sysutils/build.sh` and are installed in
`/bin` by the developer userspace image workflow. All module-management
commands require effective UID 0.
