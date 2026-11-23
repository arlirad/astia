#pragma once

#include <stdarg.h>
#include <stddef.h>

typedef unsigned long chtype;
typedef unsigned long attr_t;
typedef unsigned long mmask_t;

typedef struct astia_window {
    int rows;
    int cols;
    int top;
    int left;
    int row;
    int col;
} WINDOW;

typedef struct {
    short id;
    int   x;
    int   y;
    int   z;
    int   bstate;
} MEVENT;

extern WINDOW* stdscr;
extern WINDOW* curscr;
extern int     LINES;
extern int     COLS;

#define OK 0
#define ERR (-1)
#define TRUE 1
#define FALSE 0
#define A_NORMAL 0ul
#define A_BOLD 1ul
#define A_REVERSE 2ul
#define A_ITALIC 4ul
#define COLOR_BLACK 0
#define COLOR_RED 1
#define COLOR_GREEN 2
#define COLOR_YELLOW 3
#define COLOR_BLUE 4
#define COLOR_MAGENTA 5
#define COLOR_CYAN 6
#define COLOR_WHITE 7
#define COLOR_PAIR(number) ((attr_t) 0)
#define ALL_MOUSE_EVENTS 0ul
#define REPORT_MOUSE_POSITION 0ul
#define KEY_MOUSE 0x181
#define KEY_RESIZE 0x182
#define KEY_BACKSPACE 0x183
#define KEY_LEFT 0x184
#define KEY_RIGHT 0x185
#define KEY_UP 0x186
#define KEY_DOWN 0x187
#define KEY_HOME 0x188
#define KEY_END 0x189
#define KEY_NPAGE 0x18a
#define KEY_PPAGE 0x18b
#define KEY_IC 0x18c
#define KEY_DC 0x18d
#define KEY_ENTER 0x18e
#define KEY_BTAB 0x18f
#define KEY_EOL 0x190
#define KEY_SLEFT 0x191
#define KEY_SRIGHT 0x192
#define KEY_SNEXT 0x193
#define KEY_SPREVIOUS 0x194
#define KEY_SBEG 0x195
#define KEY_SEND 0x196
#define KEY_SHOME 0x197
#define KEY_SIC 0x198
#define KEY_SDC 0x199
#define KEY_SF 0x19a
#define KEY_SR 0x19b
#define KEY_SUSPEND 0x19c
#define KEY_SSUSPEND 0x19d
#define KEY_CANCEL 0x19e
#define KEY_SCANCEL 0x1af
#define KEY_BEG 0x19f
#define KEY_CENTER 0x1a0
#define KEY_A1 0x1a1
#define KEY_A3 0x1a2
#define KEY_B2 0x1a3
#define KEY_C1 0x1a4
#define KEY_C3 0x1a5
#define KEY_CONTROL_L 0x1a6
#define KEY_CONTROL_R 0x1a7
#define KEY_SHIFT_L 0x1a8
#define KEY_SHIFT_R 0x1a9
#define KEY_ALT_L 0x1aa
#define KEY_ALT_R 0x1ab
#define KEY_SUP 0x1ac
#define KEY_MAX 0x1ad
#define KEY_F0 0x200
#define KEY_F(number) (KEY_F0 + (number))
#ifndef IEXTEN
#define IEXTEN 0x008000
#endif
#ifndef P_tmpdir
#define P_tmpdir "/tmp"
#endif

#define getmaxyx(window, y, x) ((y) = (window)->rows, (x) = (window)->cols)
#define getyx(window, y, x) ((y) = (window)->row, (x) = (window)->col)
#define getcury(window) ((window)->row)
#define getcurx(window) ((window)->col)

WINDOW*     initscr(void);
int         endwin(void);
int         isendwin(void);
WINDOW*     newwin(int rows, int cols, int top, int left);
int         delwin(WINDOW* window);
int         wmove(WINDOW* window, int row, int col);
int         waddch(WINDOW* window, const chtype value);
int         waddnstr(WINDOW* window, const char* text, int length);
int         waddstr(WINDOW* window, const char* text);
int         wprintw(WINDOW* window, const char* format, ...);
int         mvwaddch(WINDOW* window, int row, int col, const chtype value);
int         mvwaddnstr(WINDOW* window, int row, int col, const char* text, int length);
int         mvwaddstr(WINDOW* window, int row, int col, const char* text);
int         mvwprintw(WINDOW* window, int row, int col, const char* format, ...);
int         wclrtoeol(WINDOW* window);
int         wrefresh(WINDOW* window);
int         wnoutrefresh(WINDOW* window);
int         wredrawln(WINDOW* window, int row, int count);
int         wscrl(WINDOW* window, int lines);
int         wgetch(WINDOW* window);
int         refresh(void);
int         doupdate(void);
int         raw(void);
int         noecho(void);
int         nonl(void);
int         keypad(WINDOW* window, int enabled);
int         nodelay(WINDOW* window, int enabled);
int         halfdelay(int tenths);
int         scrollok(WINDOW* window, int enabled);
int         curs_set(int visibility);
int         beep(void);
int         napms(int milliseconds);
int         has_colors(void);
int         start_color(void);
int         use_default_colors(void);
int         init_pair(short pair, short foreground, short background);
int         wattron(WINDOW* window, int attributes);
int         wattroff(WINDOW* window, int attributes);
int         mousemask(mmask_t newmask, mmask_t* oldmask);
int         mouseinterval(int milliseconds);
int         wmouse_trafo(const WINDOW* window, int* y, int* x, int to_screen);
int         wenclose(const WINDOW* window, int y, int x);
int         ungetch(int value);
const char* tigetstr(const char* capability);
char*       tgetstr(const char* capability, char** area);
int         key_defined(const char* sequence);
int         define_key(const char* sequence, int keycode);
int         typeahead(int file_descriptor);
