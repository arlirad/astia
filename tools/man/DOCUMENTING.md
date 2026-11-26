# Astia syscall, libc, and overview documentation

This file is the documentation contract for Astia syscall (section 2), native
libc filesystem/descriptor (section 3), and subsystem overview (section 7)
man pages.

## Ownership and layout

Syscall implementations remain owned by their subsystem:

```text
kernel/src/proc/syscalls/<name>.cpp
kernel/src/fs/syscalls/<name>.cpp
kernel/src/ipc/syscalls/<name>.cpp
kernel/src/mm/syscalls/<name>.hpp
kernel/src/sys/syscalls/<name>.cpp
```

Handler comments are no longer documentation sources. Do not add `///`
man-page blocks to syscall handlers. The authored documentation is:

```text
docs/2/<subsystem>/<category>/<canonical>.md
                             81 public syscall pages, IDs 0..80
docs/2/fs/<canonical>.md     fs pages
docs/2/abi/<canonical>.md    public ABI pages
docs/7/<name>.md             seven subsystem overviews
docs/manifest.json           IDs, names, slugs, sources, taxonomy, synopsis metadata
```

The manifest preserves canonical names, numeric IDs, subsystems, categories,
authored source paths, stable route slugs, synopsis text, and `public`/`raw`
kind. Every page records an explicit authored `sourcePath`; the canonical
handler or public wrapper remains in `source`. Source paths intentionally
repeat where several syscalls share an ABI header or process implementation;
the manifest records those expected duplicates.
Section 3 remains a separate, generator-owned `LIBC_PAGES` inventory and must
not be copied into `docs/2` or `docs/7`.

Section 7 keeps its existing route slugs (`docs-aio-7`, `docs-alarm-7`,
`docs-exec-7`, `docs-rcu-7`, `docs-sigtimedwait-7`, `docs-spawnve-7`,
`docs-task-7`) and `docs-7-<name>` aliases. Keep existing heading anchors
stable when editing those pages.

Design notes, README pages, section 3 pages, and `docs/aarch64-virt-boot.md`
are outside this migration. The existing AArch64 manifest drift is a separate
known issue and must not be folded into this change.

## Required man-page sections

Every authored section 2 page provides these Markdown sections:

````markdown
# name(2)

## SYNOPSIS

```c
long astia_name(...);
```

## DESCRIPTION

Explain what the syscall does and define special values such as 0 or -1.

## RETURN VALUE

Describe the successful result and whether the syscall can block.

## ERRORS

- `-EINVAL`: Explain the exact invalid input that causes this case.
- `-EPERM`: Explain the permission failure.

## NOTES

ABI, blocking, ownership, or implementation-specific behavior.
````

`sigreturn` is the only raw page. Its `SYNOPSIS` is the raw ABI notice rather
than a C declaration. Public pages must keep the manifest synopsis equal to
the code block in the authored page.

## Metadata and parity rules

`docs/manifest.json` contains no Markdown bodies. It is the authority for:

- exact public IDs `0..80` and canonical names;
- stable slugs of the form `syscall-<id>-<name-with-dashes>`;
- source handler paths, including expected shared paths;
- category and `public`/`raw` synopsis kind;
- the exact synopsis and section 7 alias metadata.

The generator validates every authored page against the manifest and the
public ABI header. It also checks the H1, `SYNOPSIS`, and `DESCRIPTION`
sections, section 7 names and aliases, relative links, and heading-anchor
uniqueness. The exported section-2 Markdown must be byte-for-byte identical to
the authored `docs/2` page. Frontend metadata must never contain `content` or
Markdown bodies.

Descriptions are sentences:

- start with an uppercase letter;
- end with exactly one period;
- explain the concrete condition;
- use ordinary ASCII `-`, never an em dash or en dash.

## Generator

Generate the static manual and the frontend metadata:

```bash
python3 tools/man/generate.py kernel/src dist/man7 --docs docs \
  --export-section2 frontend/.cache/syscall-docs
node frontend/scripts/generate-docs.mjs
```

The generator:

- reads authored `docs/2`, `docs/7`, and `docs/manifest.json`;
- validates section 2 and section 7 metadata before writing anything;
- groups the index by `MAN 2 - SYSTEM CALLS`, `MAN 3 - LIBC APIs`, and
  `MAN 7 - OVERVIEWS`;
- renders section 2 pages from authored Markdown and section 3 pages from the
  separate `LIBC_PAGES` inventory;
- copies the Nodivia-derived theme CSS;
- writes one HTML page per section 2 and section 7 entry;
- exports section-2 metadata plus verbatim authored Markdown for the frontend.

Overview and section 2 Markdown use a dependency-free subset: ATX headings,
paragraphs, fenced code blocks, and flat ordered or unordered lists. The first
level-one heading is the page title. Use `--docs PATH` only for a directory
laid out like `docs` with `2/`, `7/`, and `manifest.json`.

## Publication safety

The generator validates that its output directory is lexically inside the
repository root and opens every path component with directory-relative,
no-follow operations. It stages a complete generation in a private sibling
directory, fsyncs the staged files and manifest, then activates the tree with
one Linux `renameat2` operation:

- existing generation: `RENAME_EXCHANGE`
- first generation: `RENAME_NOREPLACE`

The active generation is not modified before activation. If staging or
activation fails, the previous manifest and pages remain active. A final
active-tree snapshot is checked immediately before activation. Any entry
added or changed after staging aborts before the directory exchange. Hash
open/read failures and entries that change type during hashing or copying
fail closed without blocking or activating. A remaining OS-level race between
the last snapshot and `renameat2` cannot be
removed without a cooperative lock; if it occurs, the retired tree is
retained as an `.astia-retired-<id>` sibling rather than deleted, preserving
unexpected files for recovery.

The retired generation is removed only after activation succeeds and its
post-exchange snapshot still matches the snapshot taken before staging.
Cleanup errors are deferred and do not turn a successful activation into a
failure. Cooperating generators should run one at a time. Staging directories
are created mode `0700`, opened and pinned immediately, and marked owned only
after the opened FD identity matches the directory entry. Cleanup uses an
atomic quarantine rename plus pinned-FD removal; uncertain identity preserves
the path and reports it. Diagnostics use lexical absolute paths and never
resolve symlinks. Staging collisions are never removed; if allocation fails
after a collision, the pre-existing paths are reported and left untouched.

Regular files are copied with owner, group, mode, ACLs, and extended
attributes preserved. Recreated directories, FIFOs, and the staging root
preserve owner, group, mode, and extended attributes, including POSIX ACLs.
Destination-inherited ACLs and xattrs are cleared before source metadata is
applied. The transaction fails closed if
metadata cannot be read or applied.

Metadata copying depends on `/proc/self/fd`. If procfs is unavailable, the
transaction fails closed before activation. Symlink metadata is not cloned.
Post-exchange verification, retired-name allocation, rename, or cleanup
failures after activation emit a RuntimeWarning with the actual retained path
(retired or existing staging) and the original cause or causes, falling back
to stderr when warnings are configured as errors; the activation itself
remains successful.

The man7 tree and the section-2 export tree are separate per-tree
transactions, not one invocation-wide atomic transaction. The frontend reads
the section-2 export only after the generator process exits successfully.

`renameat2` directory exchange requires Linux and a filesystem that supports
it for sibling directories on the same filesystem, such as ext4, XFS, Btrfs,
or tmpfs. The output directory, including its staging sibling, must remain
inside the repository root.


The index must never mix section 2 and section 3 links in a top-level manual
section or subsystem group. Every section 2 page links as `<name>.2.html` and
is titled `<name>(2)`; every section 3 page links as `<name>.3.html` and is
titled `<name>(3)`. Every section 7 overview links as `<name>.7.html` and is
titled `<name>(7)`. Pages with the same name in different sections are
distinct pages and must both remain present.

New syscall-specific documentation takes precedence over fallback metadata.
The section 3 API metadata is intentionally maintained against
`libc/include/{fcntl.h,unistd.h,sys/stat.h}` and `libc/src/{sys/unistd.c,fs/stat.c}`;
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
python3 tools/man/generate.py kernel/src dist/man7 --docs docs
grep -R "<h2>" dist/man7/index.html
find dist/man7 -name '*.7.html' -print
test "$(find dist/man7 -maxdepth 1 -name '*.7.html' | wc -l)" -eq 7
curl -fsS https://arlia.nodivia.pl/kill.html
```

## Coverage contract

The authored section 2 set includes every direct syscall exported by
`kernel/include/astia/api/syscalls.h`. A syscall must have specific synopsis,
return, error, and state semantics; a generic fallback description is not
acceptable for a newly exported call.

The generated section 3 set covers the native wrappers declared by
`libc/include/unistd.h` and `libc/include/sys/stat.h` where their behavior is
implemented in `libc/src`.  This includes `access`, `getgroups`, `setgroups`,
`chown`, `lchown`, `fchown`, the typed `file_mode` and `file_permissions`
kernel APIs, controlling-terminal process-group wrappers, and the `/dev`
devfs pseudo-device surface.  Pages must describe Astia behavior, including
intentional restrictions, rather than importing a generic POSIX contract.

Use the ASCII hyphen for prose and ABI errno spellings. Do not use em dashes,
en dashes, or non-breaking hyphens in authored documentation.

The current overview pages are:

```text
docs/7/task.md
docs/7/aio.md
docs/7/alarm.md
docs/7/exec.md
docs/7/rcu.md
docs/7/sigtimedwait.md
docs/7/spawnve.md
```
