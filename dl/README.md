# dl — the userspace ELF interpreter and dynamic loader

`ld.astia.so` is the program the kernel enters when an executable names a
`PT_INTERP`. It is freestanding: it cannot use the libc it is meant to load.
Besides relocating the startup image it provides the public `dlopen` family
declared in `libc/include/dlfcn.h`.

## What it does

1. Reads the kernel-built startup frame (`AT_PHDR`, `AT_PHNUM`, `AT_PHENT`,
   `AT_ENTRY`, `AT_BASE`, the `AT_ASTIA_TLS_*` arena, and
   `AT_ASTIA_PAYLOAD_BASE`). The load bias comes from `PT_PHDR` when the
   payload has it, otherwise from the auxv entry; a payload that offers
   neither is rejected with exit 115 instead of being biased by zero.
2. Walks the payload's `PT_DYNAMIC`: `DT_NEEDED`, `DT_STRTAB`, `DT_SYMTAB`,
   `DT_HASH`/`DT_GNU_HASH`, `DT_RELA`, `DT_JMPREL`.
3. Opens each `DT_NEEDED` under `/lib/` and maps it with `dlmap`. The kernel
   shares the image physically, and `dlmap` is identity-keyed, so a diamond
   dependency maps once.
4. Resolves `RELATIVE`, `GLOB_DAT`, `JUMP_SLOT`, `64`, `TPOFF64`/`TPOFF32`
   and `DTPMOD64`/`DTPOFF64` against the payload and its dependencies using
   `(name, version)` matching from `DT_VERSYM`/`DT_VERNEED`/`DT_VERDEF`; weak
   undefined references resolve to zero.  GNU IFUNC symbols (`STT_GNU_IFUNC`)
   resolve in two phases: relocation queues the slot and the resolver's
   address, then every resolver runs after all relocations and before any
   `DT_INIT`, and its result lands in the slot.  `R_X86_64_IRELATIVE` (a DSO
   referencing its own IFUNC) follows the same path.  A resolver may call
   ordinary functions whose relocations are already applied, but it must not
   call another IFUNC: that slot is still unpatched, so the call would land
   on the second resolver instead of its result.  Resolving such chains needs
   per-slot lazy thunks beyond the current PLT path, and remains a known
   residual limitation.
5. Binds startup `JUMP_SLOT` entries lazily by default: each module's
   `GOT[1]`/`GOT[2]` point at its module record and the per-architecture
   resolver trampoline, and the first PLT call patches its own slot. A slot
   the toolchain left at its link-time PLT stub address is biased by the load
   address when that address falls inside the executable segment. Two threads
   may race the first call and both enter the resolver; the slot patch is
   idempotent and harmless. Modules linked with `-z now` (`DT_BIND_NOW`,
   `DT_FLAGS & DF_BIND_NOW`, or `DT_FLAGS_1 & DF_1_NOW`) keep eager binding.
   `dlopen` groups are always relocated eagerly, so `RTLD_LAZY` changes
   nothing for them.
6. Lays each shared object's `PT_TLS` block out below the kernel's TLS arena,
   copies the templates after relocation, and applies the TLS relocations.
7. Makes each module's `PT_GNU_RELRO` range read-only (`.data.rel.ro` and,
   under partial RELRO, `.got`) after relocation, IFUNC resolution and TLS
   setup, before any constructor runs. Only the lazy PLT slots (`.got.plt`)
   stay writable because they lie outside the segment; `-z now` alone does
   not move them in: full RELRO is the `-z relro -z now` combination that
   places `.got.plt` inside `PT_GNU_RELRO`, where this step protects it as
   well. The range is page-rounded: start down, end up, and a wrapping
   interval fails closed. The interpreter image itself (`ld.astia.so`) is
   mapped by the kernel and is not in this base's module table, so its own
   RELRO is out of scope here.
8. Appends the interpreter itself, prelinked: it is read from `AT_BASE`, its
   headers are parsed for symbol lookup, and its relocations are skipped. It
   also exports `__tls_get_addr`, so it stays in the lookup table for DSOs
   that use global-dynamic TLS.
9. Builds a per-thread DTV lazily when a global-dynamic DSO calls
   `__tls_get_addr`; every thread gets its own blocks and a forked child
   inherits the parent's entry.
10. Runs `DT_INIT`/`DT_INIT_ARRAY` dependencies-first.
11. Jumps to `AT_ENTRY` with the original stack.

The kernel applies `RELATIVE` when it maps an image and defers symbol
relocations for `dlmap`ped images (`defer_symbol_relocs`); an `astia_dlmap`
caller therefore receives images whose GOT slots are unrelocated. Only this
program can consume that contract today.

## Public API

`<dlfcn.h>` declares:

```c
void* dlopen(const char* file, int mode);
int   dlclose(void* handle);
void* dlsym(void* handle, const char* name);
char* dlerror(void);
int   dl_iterate_phdr(int (*callback)(struct dl_phdr_info*, size_t, void*), void* data);
```

`RTLD_LAZY` and `RTLD_NOW` are accepted; `dlopen` groups are always relocated
eagerly, so `RTLD_LAZY` changes nothing for them (the fixtures are built with
`-z now`). Startup modules still lazy-bind by default.
`RTLD_LOCAL` is the default; `RTLD_GLOBAL` puts every module in the group in
the default scope; `RTLD_NODELETE` keeps the group mapped after its last
handle closes. `RTLD_DEFAULT` (NULL) tells `dlsym` to search the default
scope. A mode with any other bit set, or with `RTLD_LAZY` and `RTLD_NOW`
together, fails with a `dlerror()` message; `RTLD_NOLOAD` is recognised by
`<dlfcn.h>` but unsupported.

### Handles, scopes, and refcounts

A handle references a module and its transitive `DT_NEEDED` closure. The
closure is stored dependency-first (post-order), so every dependency appears
before its dependents and the root is last. Opening an already loaded module
(matched by soname/name or mapped base) returns a fresh handle and increments
every group member's refcount; it does not map the image again, and a normal
reopen does not initialize it again. The exception is a
finalized-but-retained module (see below), whose initializers are re-run on
reopen.
`dlsym(handle, name)` searches the root first and then the group in stored
order (dependencies first). `dlsym(RTLD_DEFAULT, name)` searches used,
non-prelinked modules that are in the global scope, in load order, and only
matches defined `STB_GLOBAL`/`STB_WEAK` symbols. If a matching symbol has type
`STT_GNU_IFUNC` its resolver is called and the result is returned (there is
no relocation slot to cache it).

While a `dlopen` group is being relocated, its symbol lookups are restricted
to the group's own dependency closure plus startup modules and modules in the
global scope. A `RTLD_LOCAL` module therefore cannot bind to an unrelated
local module that might unload later.

`dlclose` drops the handle refcount. At zero, module refcounts are decremented
for the group. Initialization runs in stored order (dependencies first), so
finalization runs in reverse order (dependents first). `DT_FINI` and
`DT_FINI_ARRAY` run only once for the candidates that will actually unload,
and the full unload decision is rechecked after the finalizers return. They
never run for `RTLD_NODELETE` or deferred images. A finalized module that is
retained because the unmap was deferred is marked `finalized`, and its handle
stays reserved; a later `dlclose` of that handle re-attempts only the unmap,
never runs `DT_FINI` again, and releases the handle once every pending module
is gone. Reopening that module re-runs its initializers, and those of any
finalized members of its retained dependency closure, dependency-first, then
clears `finalized` and `pending` before returning the new handle. Startup
modules are never unloaded. `dlerror` returns a bounded message once and
clears it; the message is kept per thread (keyed by the thread pointer, with
a shared fallback once 16 threads have reported errors), so concurrent
failures do not overwrite each other.

The loader is single-entry: while `dlopen`/`dlclose` mutate the tables, and
during every `dlsym` lookup (including an IFUNC resolver call),
`dlopen`/`dlclose`/`dlsym` fail with a `dlerror` instead of changing state.
Initializers and finalizers must not call the loader, create threads, or
install handlers. If a finalizer does create a thread or install a handler,
the post-finalizer recheck defers the unmap: the module stays mapped and
finalized. Reopening it re-initializes it as described above.

### dl_iterate_phdr

The callback runs for every used image in load order, including the prelinked
interpreter. The payload reports an empty `dlpi_name`; dependencies and
`dlopen`ed images report their soname (or path when they have no soname).
`dlpi_adds`/`dlpi_subs` are zero, and `dlpi_tls_modid`/`dlpi_tls_data` are
zero because dynamic TLS is not implemented. A non-zero callback return stops
the iteration.

### Unload quiescence

Unloading is a lifetime/safety boundary. Even with a zero refcount, a dynamic
module stays mapped unless all of these hold:

- the process has exactly one live thread (`astia_thread_count() == 1`); any
  other count defers. This is an Astia limitation: the caller must not create
  a thread between this check and `dlunmap`; a thread created in that window
  that enters the library is a caller synchronization error;
- no signal in 1..64 has a handler installed. libc owns the kernel
  dispositions and keeps user handlers in its own table, so the loader cannot
  attribute a handler to a module and treats any handler as a deferral;
- `dlclose` was not called from code inside the module
  (`__builtin_return_address(0)` of the `dlclose` caller);
- no other used module lists it in `DT_NEEDED` and no other live handle
  references it.

When a module cannot be unmapped because the process is not quiescent, it is
kept mapped, removed from the global scope, marked `pending`, and `dlclose`
returns 0. The handle remains reserved as the retry token: calling `dlclose`
on it again retries the unmap and releases the handle only after every
pending module is gone. If `astia_dlunmap` itself fails, the same pending
retry state is kept but `dlclose` records a `dlerror` message and returns
-1. A module whose finalizer has run is also marked `finalized`; reopening
it re-initializes it as described above. This conservative fallback means an
active frame or a handler can defer unload indefinitely; the process owns the
image until it exits or the same handle is closed again once quiescent.

## Scope (v1)

- lazy `JUMP_SLOT` binding for startup modules by default; `-z now` modules
  and every `dlopen` group are eager;
- breadth-first `DT_NEEDED`, transitive dependencies, diamond dedup;
- `PT_GNU_RELRO` applied after relocation/TLS and before constructors:
  `.data.rel.ro` and `.got` are read-only; full RELRO (`-z relro -z now`)
  additionally covers the lazy `.got.plt` slots; `dlopen` groups are hardened
  after their relocations and IFUNC resolution, before their constructors;
- `DT_INIT`/`DT_INIT_ARRAY` and `DT_FINI`/`DT_FINI_ARRAY`;
- static TLS for initial-exec DSOs; global-dynamic TLS through per-thread
  DTVs and `__tls_get_addr` (still laid out in the static reserve);
- symbol versioning: a versioned reference binds only to a definition
  exporting the same version name, a versioned reference never binds to an
  unversioned definition, and an unversioned reference prefers the first
  non-hidden default definition;
- at most 32 loaded modules (`LD_MAX_MODULES`), 16 live handles
  (`LD_MAX_HANDLES`), and 64-byte paths (`LD_PATH_MAX`); the kernel's
  per-process `dlmap` binding table is smaller than 32 in practice;
- one loader instance per process, single-threaded `dlopen`/`dlclose`; unload
  requires a quiescent single-threaded process;
- no `RTLD_NEXT`, SysV/GNU hash, direct dependencies only.

Per-thread DTVs are bounded (`LD_MAX_THREADS`, 16) and never retired:
thread-exit cleanup is not implemented, so the registry leaks one small entry
per distinct thread pointer. A future `dlopen` module with TLS is allocated
on first access; unloading is not supported.

A new thread inherits the creating thread's DSO-TLS values: the kernel clones
the materialized TLS arena, and the loader does not re-run templates on clone.
Template reinitialisation on clone is a kernel follow-up.

Static TLS is signal-safe: block addresses are computed from the thread
pointer and constant metadata, with no per-entry initialisation window. DTV
entries are claimed lock-free with a CAS on the thread-pointer key, so the
registry is fork-safe (a child inherits the forking thread's valid entry).
A re-entrant claim joins the existing entry rather than duplicating it.
A future `dlopen` dynamic block is allocated outside any lock and published
with a CAS; the loser unmaps its copy, and allocated blocks are never
reclaimed at thread exit. The rest of the loader (dependency loading,
relocation) is not reentrant, so this signal-safety covers the TLS lookup
path only.

Known gaps are tracked in the issue tracker under the `dl` label
(`gh issue list --label dl`): the eagerly allocated TLS reserve, and IFUNC
chains (noted above). At most 64 IFUNC relocations per process
(`LD_MAX_IFUNCS`) are supported.
A missing library or unresolved symbol exits with a non-zero status; the boot
test turns that into a failure rather than a silent jump into a null pointer.

## Layout

```
dl/ld.c                 ELF/dynamic-section core and the dlopen family
dl/ld.h                 ELF64, DT_* and registry definitions
dl/symcount.c           dynamic-symbol count from SysV/GNU hash tables
dl/symcount.h           symbol-count input used by ld.c
libc/include/dlfcn.h    public loader API
dl/arch/<arch>/start.S  entry stub: save the startup stack, run ld_main, jump
dl/arch/<arch>/resolve.S lazy PLT trampoline into ld_lazy_resolve
dl/arch/<arch>/reloc.h  relocation encoding for the architecture
dl/CMakeLists.txt       standalone build (select with -DASTIA_DL_ARCH=...)
```

The kernel test harness compiles the same sources directly; `dl/CMakeLists.txt`
is for building the interpreter on its own.
