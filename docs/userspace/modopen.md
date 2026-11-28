# modopen

Load and publish a kernel module image from userspace.

## Synopsis

```text
modopen PATH
modopen --help
modopen --version
```

`PATH` must name a readable Astia kernel module image in the caller's VFS
namespace. The image must be a relocatable `ET_REL` module accepted by the
kernel module loader.

## Output

On success, `modopen` prints the module's generation-checked identifier:

```text
SLOT:GENERATION
```

Keep this value when a later command must identify the exact module instance.
It can be passed to `modls` and `modclose`. A module name is not printed by
`modopen`.

`--help` prints the command synopsis and `--version` prints the Astia sysutils
version. Any other argument count is a usage error.

## Exit status

- `0`: The image was loaded and published.
- `1`: The kernel rejected the image or the operation failed.
- `2`: The command line is invalid.

Error details are written to standard error using the kernel errno text.
Loading requires effective UID 0. The command does not run the module entry
point; it only makes the module available in the kernel registry.

## Examples

```sh
id=$(modopen /mod/rtl8139.km) || exit $?
printf 'loaded module %s\n' "$id"
modls
```
