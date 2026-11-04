# Asynchronous ELF loading and `execve`

The process loader is stateful because executable bytes arrive asynchronously.
It does not hold a filesystem or process lock across an await, and it builds a
replacement image before publishing it as the process image.

## Streaming ELF loader

`elf::load_executable_stream()` first reads the ELF header and program-header
table through an `async_env`. It then reads each `PT_LOAD` file range in
chunks, maps the segment pages, zero-fills the memory tail, applies the
supported relative relocations, and records the entry point and load bias.
The coroutine frame preserves the current phase, offsets, pending mapping, and
read result while an I/O task is suspended.

The process uses a staging page map. Segment mappings and TLS are installed in
that map, and every failure path unmaps pending ranges and releases their
frames. Only after the complete image and its startup stack are valid does the
process adopt the map. ET_EXEC uses its fixed address; ET_DYN receives an
allocator-selected load bias. Unsupported or malformed images fail rather
than producing a partial address space.

## The `execve` continuation

The syscall copies the path, argument vector, and environment vector into an
`exec_continuation` before starting asynchronous work. The continuation owns
the retained process, thread, file, staged arguments, and runner task. The
runner awaits `process::exec_user_file_async()`, which performs the streaming
load and startup-stack construction.

While the runner waits for I/O, the syscall continuation parks the calling
thread and the scheduler can run other work. Completion records either the
negative errno or the new entry and stack. On success, close-on-exec
descriptors are processed, the old image is replaced, and the saved syscall
frame is changed to enter the new image. On failure, the old image remains in
place and the continuation returns the error.

The continuation allocation, staged argument copy, task-frame allocation, and
image mappings are independent failure points. Each must be checked before
the old process image is changed.
