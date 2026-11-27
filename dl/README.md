# dl — the userspace ELF interpreter

`ld.astia.so` is the program the kernel enters when an executable names a
`PT_INTERP`. It is freestanding: it cannot use the libc it is meant to load.

## What it does

1. Reads the kernel-built startup frame (`AT_PHDR`, `AT_PHNUM`, `AT_PHENT`,
   `AT_ENTRY`, the `AT_ASTIA_TLS_*` arena, and `AT_ASTIA_PAYLOAD_BASE`).
   The load bias comes from `PT_PHDR` when the payload has it, otherwise from
   the auxv entry; a payload that offers neither is rejected with exit 115
   instead of being biased by zero.
2. Walks the payload's `PT_DYNAMIC`: `DT_NEEDED`, `DT_STRTAB`, `DT_SYMTAB`,
   `DT_HASH`/`DT_GNU_HASH`, `DT_RELA`, `DT_JMPREL`.
3. Opens each `DT_NEEDED` under `/lib/` and maps it with `dlmap`. The kernel
   shares the image physically, and `dlmap` is identity-keyed, so a diamond
   dependency maps once.
4. Resolves `RELATIVE`, `GLOB_DAT`, `JUMP_SLOT`, `64` and `TPOFF64`/`TPOFF32`
   against the payload and its dependencies; weak undefined references
   resolve to zero.  GNU IFUNC symbols (`STT_GNU_IFUNC`) resolve in two
   phases: relocation queues the slot and the resolver's address, then every
   resolver runs after all relocations and before any `DT_INIT`, and its
   result lands in the slot.  `R_X86_64_IRELATIVE` (a DSO referencing its own
   IFUNC) follows the same path.  A resolver may call ordinary functions
   whose relocations are already applied, but it must not call another
   IFUNC: that slot is still unpatched, so the call would land on the second
   resolver instead of its result.  Resolving such chains needs the per-slot
   lazy thunk mechanism tracked with lazy PLT binding (#163), and remains a
   known residual limitation.
5. Runs `DT_INIT`/`DT_INIT_ARRAY` dependencies-first.
6. Lays each shared object's `PT_TLS` block out below the kernel's TLS arena,
   copies the templates after relocation, and applies the TLS relocations.
7. Jumps to `AT_ENTRY` with the original stack.

The kernel applies `RELATIVE` when it maps an image and defers symbol
relocations for `dlmap`ped images (`defer_symbol_relocs`); an `astia_dlmap`
caller therefore receives images whose GOT slots are unrelocated. Only this
program can consume that contract today.

## Scope (v1)

- eager binding (`-z now`); no lazy PLT;
- breadth-first `DT_NEEDED`, transitive dependencies, diamond dedup;
- `DT_INIT`/`DT_INIT_ARRAY` (dependencies first); `DT_FINI` is left to process
  teardown;
- static TLS only: build DSOs with `-ftls-model=initial-exec`;
- at most 8 loaded modules (`LD_MAX_MODULES`) and 7 `DT_NEEDED` per payload.

Known gaps are tracked in the issue tracker under the `dl` label
(`gh issue list --label dl`): lazy PLT binding, symbol versioning,
global-dynamic TLS, `dlopen`/`dlclose` scopes and unload quiescence, RELRO, and
the eagerly allocated TLS reserve.  At most 64 IFUNC relocations per process
(`LD_MAX_IFUNCS`) are supported.

A missing library or unresolved symbol exits with a non-zero status; the boot
test turns that into a failure rather than a silent jump into a null pointer.

## Layout

```
dl/ld.c                 ELF/dynamic-section core (architecture independent)
dl/ld.h                 ELF64 and DT_* definitions this program needs
dl/arch/<arch>/start.S  entry stub: save the startup stack, run ld_main, jump
dl/arch/<arch>/reloc.h  relocation encoding for the architecture
dl/CMakeLists.txt       standalone build (select with -DASTIA_DL_ARCH=...)
```

The kernel test harness compiles the same sources directly; `dl/CMakeLists.txt`
is for building the interpreter on its own.
