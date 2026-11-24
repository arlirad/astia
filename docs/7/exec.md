# Asynchronous ELF loading and `execve`

## NAME

`execve(2)`, `elf::load_executable_stream()` - replace a process image from an
ELF file while kernel I/O is asynchronous.

## SYNOPSIS

```c
long execve(const char *path, char *const argv[], char *const envp[]);
```

```cpp
core::task<elf::image> elf::load_executable_stream(
    elf::async_env, usize file_size);
```

## DESCRIPTION

`execve` is synchronous from userspace but asynchronous inside the kernel. The
syscall does not return while its runner waits for file I/O; the calling kernel
thread yields and the scheduler runs other work. The loader never publishes a
partial replacement image.

## OPERATION/DATA FLOW

`elf::load_executable_stream()` reads the ELF header and program-header table,
validates the image, then reads each `PT_LOAD` range in 4096-byte chunks. It
maps the complete span, zero-fills its memory tail, applies supported
`R_X86_64_RELATIVE` relocations, reads the static TLS template, and records the
entry point and load bias. ET_EXEC uses its fixed address; ET_DYN receives an
allocator-selected bias.

The process first builds a staging page map. Segment pages, TLS, and the
startup stack are prepared there. Failed reads, malformed metadata, unsupported
relocations, mapping failures, and frame failures unmap pending ranges and
release their frames. Only a complete image is adopted.

The syscall copies the path, argument vector, and environment vector into an
`exec_continuation`. Its runner awaits
`process::exec_user_file_async()`. When loading and stack construction succeed,
close-on-exec descriptors are processed, the old image is replaced, and the
saved syscall frame is changed to the new entry point and stack. On failure,
the old image remains in place and the syscall returns the error.

## LIFETIME/OWNERSHIP

The continuation retains the process, calling thread, open file, staged
arguments, and runner task until completion. The staging process owns the new
page map and mappings before replacement. The coroutine frame preserves loader
state, offsets, pending mappings, and read results across awaits.

The old image is not changed until the staged image and startup stack are
valid. Cleanup on every failure path is required before the continuation and
its retained references are released.

## ERRORS

Invalid or inaccessible path, argument, and environment pointers return
`-EFAULT`. Continuation, argument-staging, coroutine-frame, page-map, physical
frame, or image allocation failures return `-ENOMEM`. Missing files return
`-ENOENT`; empty or invalid executable files return `-ENOEXEC` or the loader's
mapped `-EINVAL`. File and device failures propagate their negative errno.

No error path changes the old process image. A successful `execve` does not
return to the old userspace instruction; it enters the new image with its new
entry point and startup stack.

## ELF API INVENTORY

The ELF core parser and validation primitives live in `elf.cpp`. The ET_REL
loader lives in `reloc_loader.cpp`; ET_EXEC/ET_DYN image loaders live in
`exec_loader.cpp`. All loaders share the private on-disk parser definitions;
there is no second parser.

| Entry point | Production callsites | Test coverage |
| --- | --- | --- |
| `elf::kind_of` | ELF probing helpers | `kernel/tests/elf_test.cpp` |
| `elf::load_relocatable` | reached through `elf::load` by `mod/module.cpp` | ET_REL relocation tests |
| `elf::load_executable` | `process::load_user_image` | contiguous executable tests |
| `elf::load_executable_stream` | `process::load_user_file_async` | bounded, malformed, truncated, cleanup and relocation tests |
| `elf::load` | `mod/module.cpp`; generic test dispatch | ET_REL and executable dispatch tests |

The former `proc::load_elf` declaration and implementation were dead APIs with
no callers. They were removed instead of wrapping the contiguous loader with a
full-file allocation. File-backed executable loading uses the bounded streaming
entry point above.

## NOTES

The streaming path supports ELF64 ET_EXEC and ET_DYN images and only the
relocation forms accepted by the loader. TLS is materialized for the new
process. Argument copying and every allocation are completed and checked before
close-on-exec processing or image publication.
