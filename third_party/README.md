# Vendored decoder sources

These trees are imported third-party sources. They keep their upstream
licenses and are excluded from the repository's own C/C++ style rules because
they are not Astia code.

- `zstd/` - Zstandard 1.5.7 decompressor, generated from
  `build/single_file_libs/zstddeclib-in.c` with
  `create_single_file_decoder.sh`. Licensed BSD-3-Clause OR GPL-2.0; Astia
  uses it under BSD-3-Clause (see `zstd/LICENSE`).
- `miniz/` - miniz `tinfl` raw-DEFLATE decoder (upstream
  `miniz_tinfl.{c,h}` and `miniz_common.h`). Licensed MIT (see
  `miniz/LICENSE`).
- `compat/` - Astia-owned freestanding declarations for the C library
  functions the decoder sources include. They declare the kernel's existing
  `memcpy`/`memmove`/`memset`/`memcmp` symbols and make `assert` inert.

Local changes are limited to the decoder boundary:

- `zstd/zstddeclib.c`: `ZSTD_malloc`/`ZSTD_calloc`/`ZSTD_free` route to
  `astia_zstd_*`, which fail closed. Decompression uses a static context and
  an exact-size caller buffer, so the supported path never allocates.
- `miniz/miniz_tinfl.c`: includes `miniz_tinfl.h` directly instead of the
  upstream umbrella `miniz.h`; only the inflate subset is vendored.
- `miniz/miniz_export.h`: empty export macro for a static build.

The decoder wrapper in `kernel/src/initrd/decompress.cpp` is the only code
that includes these headers.
