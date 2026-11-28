# modls

List the published kernel modules visible to the module registry.

## Synopsis

```text
modls
modls --help
modls --version
```

With no arguments, `modls` takes one point-in-time snapshot and prints one
line for each published module. Loading and unloading entries that are not
yet published are not included.

## Output

For a non-empty registry, the first line is a column header:

```text
ID STATE FLAGS REFS DEPS SIZE NAME
```

Each module line contains:

- `ID`: the generation-checked `slot:generation` identifier.
- `STATE`: the module state, such as `loaded`, `running`, or `exiting`.
- `FLAGS`: comma-separated module flags, or `none`.
- `REFS`: active reference count.
- `DEPS`: dependency count.
- `SIZE`: image size in bytes.
- `NAME`: the module's bounded registry name.

An empty registry prints `no modules`. If the registry contains more entries
than the command's fixed list capacity, a final line reports how many entries
were shown and the total count.

`--help` prints the command synopsis and `--version` prints the Astia sysutils
version. Any other argument is a usage error.

## Exit status

- `0`: The snapshot was printed, including an empty snapshot.
- `1`: The kernel rejected the query.
- `2`: The command line is invalid.

Listing requires effective UID 0. Error details are written to standard error.
The output is a snapshot; a module can change state immediately after it is
listed.

## Examples

```sh
modls
```

Use the `ID` column as the unambiguous argument to `modclose`:

```sh
modclose 3:7
```
