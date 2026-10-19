# Kernel debug symbols

The panic path deliberately prints raw addresses so it remains safe and
independent of a runtime symbol allocator. Resolve a captured trace against
the exact kernel ELF that produced it:

```sh
kernel/src/dbg/symbolize-trace.sh \
  kernel/build-tests/src/astiakrn_runtime \
  /tmp/qemu.log
```

This uses the kernel ELF's DWARF/symbol table and prints function names and
source locations. Module symbols are a separate ET_REL loader concern; they
are not automatically the kernel's symbol table.
