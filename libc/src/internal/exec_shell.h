#pragma once

#pragma GCC visibility push(default)

/* Shell execvp() runs an ENOEXEC candidate through.  Host tests point this at
 * a missing path to exercise the no-shell branch; NULL or "" means no shell. */
extern const char* __astia_execvp_shell;

#pragma GCC visibility pop
