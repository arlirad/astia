# Nano port blocker

The pinned GNU nano 8.6 source was configured from a private staging copy with
NLS, UTF-8, iconv, gettext, xattr, dlopen, and pkg-config optional paths
disabled. Configuration stops because Astia provides neither a curses header
nor a curses/ncurses terminal library:

```
configure: error: No curses lib was found.
```

Nano is therefore not built or packaged. Adding a fake curses result or a host
library would not produce an Astia binary. A future port requires a complete
terminal library and its terminfo/runtime support before nano can be enabled.
