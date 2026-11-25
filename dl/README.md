#dl — the userspace ELF interpreter

`ld.astia.so` is the program the kernel enters when an executable names a
`PT_INTERP`.  It is freestanding: it cannot use the libc it is meant to load.

## What it does

1. Reads the kernel-built startup frame (`AT_PHDR`, `AT_PHNUM`, `AT_PHENT`,
   `AT_ENTRY`, `AT_BASE`).
2. Walks the payload's `PT_DYNAMIC`: `DT_NEEDED`, `DT_STRTAB`, `DT_SYMTAB`,
   `DT_HASH`, `DT_RELA`, `DT_JMPREL`.
3. Opens each `DT_NEEDED` under `/lib/` and maps it with `dlmap`.  The kernel
   shares the image physically, so the same library is loaded once per boot.
4. Resolves `GLOB_DAT`/`JUMP_SLOT`/`64` relocations against the payload and
   its dependencies, applies them, and jumps to `AT_ENTRY` with the original
   stack.

The kernel applies the `RELATIVE` relocations when it maps an image, and
refuses `GLOB_DAT`/`JUMP_SLOT` only for images with no interpreter; with a
`PT_INTERP` it leaves those for this program.

## Scope (v1)

- Eager binding only (`-z now` semantics);
no lazy PLT.- `DT_INIT`/`DT_INIT_ARRAY` run dependencies - first;
`DT_FINI` is left to process teardown.Weak undefined references resolve to zero.-
    SysV and GNU hash tables are both                                      read;
symbol versioning and `IFUNC` are not .- `DT_NEEDED` is walked breadth - first,
    so transitive dependencies and diamond graphs work; there is no global symbol scope beyond "payload first, then
  load order".
- Static TLS for shared objects uses the initial-exec model.  The kernel
  reserves 64 KiB below the payload's own TLS block (`AT_ASTIA_TLS_*`), the
  interpreter lays each DSO's `PT_TLS` out downward from there, copies the
  templates, and applies `R_X86_64_TPOFF64`/`TPOFF32`.  Build DSOs with
  `-ftls-model=initial-exec`;
global - dynamic TLS(`DTPMOD64`/`DTPOFF64`,
  `__tls_get_addr`) is rejected.-
    Images mapped through `dlmap` have their `GLOB_DAT`/`JUMP_SLOT`/`64` relocations
        deferred to this program(`defer_symbol_relocs`); the kernel
  still rejects them for static images, where no linker will ever run.
- No `dlopen`/`dlclose`; the kernel's `dlmap`/`dlunmap` stay the only handle
  API, and the linker is free to grow a public one later.

A missing library or unresolved symbol exits with status 127, which the boot
test turns into a failure instead of a silent jump into a null pointer.

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
