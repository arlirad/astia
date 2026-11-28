# modclose

Quiesce and unload a published kernel module.

## Synopsis

```text
modclose MODULE
modclose --help
modclose --version
```

`MODULE` is either the exact module name or the generation-checked
`slot:generation` identifier printed by `modopen` and `modls`. An identifier
is preferred when more than one module instance can have the same name.

The command first resolves a name through a module snapshot. It then asks the
kernel to quiesce the selected module, wait for active references and
dependents to drain, and unmap it. A running or resident module cannot be
closed.

## Output

On success, `modclose` prints:

```text
closed SLOT:GENERATION
```

`--help` prints the command synopsis and `--version` prints the Astia sysutils
version. Any other argument count is a usage error.

## Exit status

- `0`: The module was closed and unloaded.
- `1`: The module could not be resolved or the kernel rejected the close.
- `2`: The command line is invalid.

Closing requires effective UID 0. Error details are written to standard
error. A missing name, stale identifier, or ambiguous name is an error and
does not close any module.

## Examples

Close the exact instance returned by `modopen`:

```sh
module_id=$(modopen /mod/example.km) || exit $?
modclose "$module_id"
```

Close by name when the name resolves to exactly one published module:

```sh
modclose example
```
