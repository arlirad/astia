# Nano port status

The pinned GNU nano 8.6 source is configured from a private staging copy with
NLS, UTF-8, iconv, gettext, xattr, libmagic, dlopen, mouse, browser, and
multibuffer optional paths disabled. A tree-local ANSI terminal compatibility
library supplies the required curses API and links as `libncurses.a`.

The port is verified by the tree-local QEMU PTY smoke: nano edits and saves
`/tmp/nano-smoke.txt`, and the supervisor checks the exact saved bytes. Host
curses libraries are never used. Mouse, UTF-8, color, browser, multibuffer,
libmagic, NLS, iconv, gettext, xattr, and dynamic loading remain intentionally
disabled; those features are not claimed by this port.
