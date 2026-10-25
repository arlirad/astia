# Astia syscall and libc documentation

This file is the documentation contract for Astia syscall (section 2) and
native libc filesystem/descriptor (section 3) man pages.

## Ownership and layout

Syscalls belong to the subsystem that owns their implementation:

```text
kernel/src/proc/syscalls/<name>.cpp
kernel/src/proc/syscalls/<name>.hpp
kernel/src/fs/syscalls/<name>.cpp
kernel/src/fs/syscalls/<name>.hpp
kernel/src/ipc/syscalls/<name>.cpp
kernel/src/sys/syscalls/<name>.cpp
```

The implementation file owns the behavior. The header owns the public
kernel-side prototype and the man-page comment. The dispatcher only registers
the handler; it must not duplicate the documentation.

## Required man-page sections

Every syscall page should provide these sections when applicable:

```text
Synopsis:
    long example(...);

Description:
    Explain what the syscall does and define special values such as 0 or -1.

Return Value:
    Describe the successful result and whether the syscall can block.

Errors:
    -EINVAL: Explain the exact invalid input that causes this case.
    -EPERM: Explain the first independent permission failure.
    -EPERM: Explain the second independent permission failure.

Notes:
    ABI, blocking, ownership, or implementation-specific behavior.
```

The generator turns `Errors:` entries into a top-level error list. Each
explanation is rendered as a nested list below its errno:

```html
<ul>
  <li>
    <code>-EPERM</code>
    <ul>
      <li>The target is a session leader.</li>
    </ul>
  </li>
</ul>
```

## Error documentation rules

Describe causes, not just the errno name. A reader should be able to answer:

> What exact input or process state produced this error?

Use one entry per independent cause. Repeating an errno is intentional and
correct when the same errno can result from different conditions:

```text
Errors:
    -EPERM: The caller is not the target's parent.
    -EPERM: The target belongs to a different session.
    -EPERM: The target is a session leader.
```

Do not collapse these into `-EPERM: Permission denied.` The detailed cases are
the useful part of the page.

Descriptions are sentences:

- start with an uppercase letter;
- end with exactly one period;
- explain the concrete condition;
- use ordinary ASCII `-`, never an em dash or en dash.

Good:

```text
-EFAULT: The status pointer does not reference writable userspace memory.
-EINVAL: The offset is not aligned to the page size.
-ESRCH: No live process belongs to the selected process group.
```

Bad:

```text
-EINVAL
-EPERM: Permission denied
-ESRCH - process missing
```

## Generator

Generate the site from the complete kernel source tree:

```bash
python3 tools/man7/generate.py kernel/src dist/man7
```

The generator:

- reads documented subsystem syscall headers;
- discovers legacy handlers in `syscalls.cpp` files;
- groups the index by subsystem;
- emits one HTML page per syscall;
- copies the Nodivia-derived theme CSS;
- supplies fallback return/error metadata for legacy handlers.
- adds the native filesystem/descriptor APIs from the libc source root as
  section 3 pages, grouped separately from section 2 syscall pages;
- links each generated page back to the combined manual index.

New syscall-specific documentation takes precedence over fallback metadata.
The section 3 API metadata is intentionally maintained against
`libc/include/{fcntl.h,unistd.h,sys/stat.h}` and `libc/src/{unistd.c,stat.c}`;
it must not imply POSIX behavior that the current native implementation does
not provide.

## Visual style

The generated pages use the local Nodivia-derived theme in:

```text
tools/man7/nodivia.css
```

Keep the man-page structure semantic. Use headings for sections, `<pre>` for
synopses, and nested lists for error explanations. Do not encode section
content as one large paragraph when it is naturally a list.

## Deployment

The deploy target is `arlia` in the deployment repository. Its build command
must generate from `kernel/src`, not only `kernel/src/proc`, so `proc`, `fs`,
`ipc`, and `sys` pages are included.

The deployed static root is:

```text
/srv/nodivia/arlia/current
```

The Arbiter site configuration is:

```text
/etc/arbiter/sites/arlia.yaml
```

After changing documentation, verify:

```bash
python3 tools/man7/generate.py kernel/src /tmp/astia-man7
grep -R "<h2>" /tmp/astia-man7/index.html
curl -fsS https://arlia.nodivia.pl/kill.html
```
