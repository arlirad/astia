# modstat

Show a snapshot of one published kernel module.

## Synopsis

```text
modstat MODULE
modstat --help
modstat --version
```

`MODULE` is either the exact module name or the generation-checked
`slot:generation` identifier printed by `modopen` and `modls`. Names must
resolve to exactly one published module.

## Output

On success, `modstat` prints:

```text
name: NAME
id: SLOT:GENERATION
state: STATE
flags: FLAGS
image-size: BYTES
active-refs: COUNT
dependents: COUNT
dependencies: COUNT
  SLOT:GENERATION
```

The final indented lines identify the module's direct dependencies. The
snapshot is point-in-time and can become stale immediately after it is
printed.

`--help` prints the command synopsis and `--version` prints the Astia sysutils
version. Any other argument count is a usage error.

## Exit status

- `0`: The module snapshot was printed.
- `1`: The name or identifier could not be resolved, or the kernel rejected the query.
- `2`: The command line is invalid.

Inspection requires effective UID 0. Error details are written to standard
error. A stale identifier never aliases a later module instance because the
generation is checked by the kernel.

## Examples

```sh
modstat 3:7
modstat ext4
```
