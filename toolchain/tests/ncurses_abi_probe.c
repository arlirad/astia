#include <ncurses_dll.h>

#include <curses.h>
#include <stddef.h>
#include <term.h>
#include <termios.h>
#include <unctrl.h>

_Static_assert(PARMRK == 0x000008, "PARMRK must match the termios ABI");
_Static_assert(ISTRIP == 0x000020, "ISTRIP must match the termios ABI");
_Static_assert(NOFLSH == 0x000080, "NOFLSH must match the termios ABI");
_Static_assert(IEXTEN == 0x008000, "IEXTEN must match the termios ABI");

#if NCURSES_WIDECHAR
#error "the Astia ncurses package must be built without wide-character support"
#endif
#if defined(NCURSES_EXT_COLORS)
#error "the Astia ncurses package must be built without extended colors"
#endif

static int (*volatile termcap_tgetent)(char*, const char*)    = tgetent;
static char* (*volatile termcap_tgetstr)(const char*, char**) = tgetstr;

int main(void) {
    SCREEN* screen;

    int key;

    if (termcap_tgetent == NULL || termcap_tgetstr == NULL)
        return 3;

    screen = newterm("vt220", stdout, stdin);
    if (screen == NULL)
        return 4;

    set_term(screen);
    cbreak();
    noecho();
    waddstr(stdscr, "NCURSES_ABI_OK");
    wrefresh(stdscr);

    key = wgetch(stdscr);

    endwin();
    delscreen(screen);

    return key < 0 ? 5 : 0;
}
