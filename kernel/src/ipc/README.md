# IPC

Kernel-side inter-process communication primitives live here:

- `pipe.cpp/.hpp` — anonymous pipe transport used by processes and AIO;
- `futex.cpp` — userspace-word wait/wake synchronization.

Process lifecycle, signal delivery, and descriptor tables remain in `proc`; the
IPC directory owns the transport/synchronization implementations they use.
