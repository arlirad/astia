#include <termcap.h>

/*
 * Minimal termcap surface.  The tty currently exposes termios and ANSI byte
 * streams, but has no terminfo database.  Returning "not available" keeps
 * non-interactive programs and zsh's basic line mode functional without
 * pretending that a terminal capability database exists.
 */
int tgetent(char* buffer, const char* terminal) {
    (void) buffer;
    (void) terminal;

    return 0;
}

int tgetflag(const char* capability) {
    (void) capability;

    return -1;
}

int tgetnum(const char* capability) {
    (void) capability;

    return -1;
}

char* tgetstr(const char* capability, char** area) {
    (void) capability;
    (void) area;

    return (char*) 0;
}

char* tgoto(const char* capability, int column, int row) {
    (void) capability;
    (void) column;
    (void) row;

    return (char*) 0;
}

int tputs(const char* string, int count, int (*putc_fn)(int)) {
    (void) string;
    (void) count;
    (void) putc_fn;

    return -1;
}
