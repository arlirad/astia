# Astia syscall, libc, and overview documentation

This file is the documentation contract for Astia syscall (section 2), native
libc filesystem/descriptor (section 3), and subsystem overview (section 7)
man pages.

## Ownership and layout

Syscalls belong to the subsystem that owns their implementation:

```text
kernel/src/proc/syscalls/<name>.cpp
kernel/src/proc/syscalls/<name>.hpp
kernel/src/fs/syscalls/<name>.cpp
kernel/src/fs/syscalls/<name>.hpp
kernel/src/ipc/syscalls/<name>.cpp
kernel/src/mm/syscalls/<name>.hpp
kernel/src/net/syscalls/<name>.hpp
kernel/src/sys/syscalls/<name>.cpp
kernel/src/sys/syscalls/<name>.hpp
```

The implementation file owns the behavior. The header owns the public
kernel-side prototype, CRTP handler, and man-page comment. Handler-only headers
are valid when behavior is implemented in an existing subsystem source; they
retain the same subsystem `syscalls/` path so documentation grouping and links
remain stable. The dispatcher only registers the handler; it must not duplicate
the documentation.

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
python3 tools/man/generate.py kernel/src dist/man7 --docs docs
```

The generator:

- reads documented subsystem syscall headers;
- groups the index first by manual section (`MAN 2 - SYSTEM CALLS`,
  `MAN 3 - LIBC APIs`, and `MAN 7 - OVERVIEWS`), then by subsystem and
  optional subgroup;
- emits one HTML page per syscall;
- copies the Nodivia-derived theme CSS;
- adds the native filesystem/descriptor APIs from the libc source root as
  section 3 pages, grouped separately from section 2 syscall pages;
- links each generated page back to the combined manual index.
- reads section 7 overview pages from the repository `docs/*.md` directory
  (including the `*.7.md` source overviews) and writes them as `<name>.7.html`
  pages in the `MAN 7 - OVERVIEWS` index group.

Overview Markdown intentionally uses a dependency-free subset: ATX headings,
paragraphs, fenced code blocks, and flat ordered or unordered lists. The first
level-one heading is the page title. Inline backtick code is escaped and
rendered as `<code>`; other inline Markdown is not interpreted. Use `--docs
PATH` to generate from another overview-document directory.

The index must never mix section 2 and section 3 links in a top-level manual
section or subsystem group. Every section 2 page links as `<name>.2.html` and
is titled `<name>(2)`; every section 3 page links as `<name>.3.html` and is
titled `<name>(3)`. Every section 7 overview links as `<name>.7.html` and is
titled `<name>(7)`. Pages with the same name in different sections are
distinct pages and must both remain present.

New syscall-specific documentation takes precedence over fallback metadata.
The section 3 API metadata is intentionally maintained against
`libc/include/{fcntl.h,unistd.h,sys/stat.h}` and `libc/src/{unistd.c,stat.c}`;
it must not imply POSIX behavior that the current native implementation does
not provide.

## Visual style

The generated pages use the local Nodivia-derived theme in:

```text
tools/man/nodivia.css
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
python3 tools/man/generate.py kernel/src /tmp/astia-man7 --docs docs
grep -R "<h2>" /tmp/astia-man7/index.html
find /tmp/astia-man7 -name '*.7.html' -print
test "$(find /tmp/astia-man7 -maxdepth 1 -name '*.7.html' | wc -l)" -eq 7
curl -fsS https://arlia.nodivia.pl/kill.html
```

## Coverage contract

The generated section 2 set includes every direct syscall exported by
`kernel/include/astia/api/syscalls.h`, including credential and group calls
implemented in `kernel/src/proc/syscalls/process/credentials.cpp`.  A syscall
must have specific synopsis, return, error, and state semantics; the generic
legacy description is not acceptable for a newly exported call.

The generated section 3 set covers the native wrappers declared by
`libc/include/unistd.h` and `libc/include/sys/stat.h` where their behavior is
implemented in `libc/src`.  This includes `access`, `getgroups`, `setgroups`,
`chown`, `lchown`, `fchown`, the typed `file_mode` and `file_permissions`
kernel APIs, controlling‑terminal process‑group wrappers, and the `/dev`
devfs pseudo‑device surface.  Pages must describe Astia behavior, including
intentional restrictions, rather than importing a generic POSIX contract.

Use a non‑breaking hyphen in prose where a compound term needs punctuation.
The ASCII hyphen remains reserved for ABI errno spellings such as `-EINVAL`.

The current overview pages are:

```text
docs/task.7.md
docs/aio.7.md
docs/alarm.7.md
docs/exec.7.md
docs/rcu.7.md
docs/sigtimedwait.7.md
docs/spawnve.7.md
```
