# TTY input injection boundary

Astia does not expose `TIOCSTI` in its userspace ABI. POSIX specifies terminal input processing and job-control access, but does not standardize `TIOCSTI`; Linux treats it as a nonportable ioctl and can require `CAP_SYS_ADMIN`.

Host tty tests define `ASTIA_TTY_HOST_TEST` and use the test-only `tty::inject_input_for_test()` helper. The helper enters the normal line discipline and readiness path without adding a syscall, ioctl request, libc declaration, or production entry point. Integration coverage uses the UART loopback path and existing fd aliases.

If a public `TIOCSTI` is added later, authorization must verify that the descriptor names the caller's controlling terminal, the caller belongs to the terminal's session and foreground process group, and the caller satisfies an explicit privileged-injection policy. The check must happen before line-discipline mutation and return `EPERM` when the security boundary is not met.
