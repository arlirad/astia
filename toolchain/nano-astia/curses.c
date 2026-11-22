#include "curses.h"

#include "libgen.h"
#include <sys/ioctl.h>

#include <errno.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

WINDOW* stdscr;
WINDOW* curscr;
int     LINES = 24;
int     COLS  = 80;

static struct termios saved_termios;
static int            have_saved_termios;
static int            pushed_key = -1;
static int            input_nonblocking;

const char* getprogname(void) {
    return "nano";
}

char* basename(char* path) {
    size_t length;
    char*  slash;
    if (path == NULL || *path == '\0')
        return ".";
    length = strlen(path);
    while (length > 1 && path[length - 1] == '/')
        path[--length] = '\0';
    slash = strrchr(path, '/');
    return slash == NULL ? path : slash + 1;
}

char* dirname(char* path) {
    size_t length;
    char*  slash;
    if (path == NULL || *path == '\0')
        return ".";
    length = strlen(path);
    while (length > 1 && path[length - 1] == '/')
        path[--length] = '\0';
    slash = strrchr(path, '/');
    if (slash == NULL)
        return ".";
    if (slash == path)
        slash[1] = '\0';
    else
        *slash = '\0';
    return path;
}

static void output(const char* text) {
    (void) write(STDOUT_FILENO, text, strlen(text));
}

static void position(int row, int col) {
    char sequence[32];
    snprintf(sequence, sizeof sequence, "\033[%d;%dH", row + 1, col + 1);
    output(sequence);
}

static int read_byte(unsigned char* value) {
    if (input_nonblocking) {
        struct pollfd descriptor = {STDIN_FILENO, POLLIN, 0};
        if (poll(&descriptor, 1, 0) != 1)
            return -1;
    }
    long result = read(STDIN_FILENO, value, 1);
    return result == 1 ? 0 : -1;
}

static int next_escape_byte(unsigned char* value) {
    struct pollfd descriptor  = {STDIN_FILENO, POLLIN, 0};
    int           poll_result = poll(&descriptor, 1, 30);
    if (poll_result != 1)
        return -1;
    return read_byte(value);
}

WINDOW* initscr(void) {
    struct winsize size;
    stdscr = calloc(1, sizeof *stdscr);
    if (stdscr == NULL)
        return NULL;
    stdscr->rows = LINES;
    stdscr->cols = COLS;
    curscr       = stdscr;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &size) == 0) {
        if (size.ws_row > 0)
            LINES = stdscr->rows = size.ws_row;
        if (size.ws_col > 0)
            COLS = stdscr->cols = size.ws_col;
    }
    output("\033[?25h\033[2J\033[H");
    return stdscr;
}

int endwin(void) {
    if (have_saved_termios)
        tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);
    output("\033[0m\033[?25h\033[2J\033[H");
    free(stdscr);
    stdscr             = NULL;
    curscr             = NULL;
    have_saved_termios = 0;
    return OK;
}

WINDOW* newwin(int rows, int cols, int top, int left) {
    WINDOW* window = calloc(1, sizeof *window);
    if (window == NULL)
        return NULL;
    window->rows = rows;
    window->cols = cols;
    window->top  = top;
    window->left = left;
    return window;
}

int delwin(WINDOW* window) {
    if (window != stdscr)
        free(window);
    return OK;
}

int wmove(WINDOW* window, int row, int col) {
    if (window == NULL || row < 0 || col < 0)
        return ERR;
    window->row = row < window->rows ? row : window->rows - 1;
    window->col = col < window->cols ? col : window->cols - 1;
    position(window->top + window->row, window->left + window->col);
    return OK;
}

int waddch(WINDOW* window, const chtype value) {
    char character = (char) (value & 0xff);
    if (window == NULL)
        return ERR;
    (void) write(STDOUT_FILENO, &character, 1);
    if (character == '\n') {
        window->row++;
        window->col = 0;
    }
    else if (++window->col >= window->cols) {
        window->col = 0;
        if (window->row + 1 < window->rows)
            window->row++;
    }
    return OK;
}

int waddnstr(WINDOW* window, const char* text, int length) {
    size_t count = length < 0 ? strlen(text) : (size_t) length;
    for (size_t i = 0; i < count; ++i)
        waddch(window, (unsigned char) text[i]);
    return OK;
}

int waddstr(WINDOW* window, const char* text) {
    return waddnstr(window, text, -1);
}

static int vprint(WINDOW* window, const char* format, va_list arguments) {
    char buffer[1024];
    vsnprintf(buffer, sizeof buffer, format, arguments);
    return waddstr(window, buffer);
}

int wprintw(WINDOW* window, const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int result = vprint(window, format, arguments);
    va_end(arguments);
    return result;
}

int mvwaddch(WINDOW* window, int row, int col, const chtype value) {
    wmove(window, row, col);
    return waddch(window, value);
}

int mvwaddnstr(WINDOW* window, int row, int col, const char* text, int length) {
    wmove(window, row, col);
    return waddnstr(window, text, length);
}

int mvwaddstr(WINDOW* window, int row, int col, const char* text) {
    return mvwaddnstr(window, row, col, text, -1);
}

int mvwprintw(WINDOW* window, int row, int col, const char* format, ...) {
    va_list arguments;
    wmove(window, row, col);
    va_start(arguments, format);
    int result = vprint(window, format, arguments);
    va_end(arguments);
    return result;
}

int wclrtoeol(WINDOW* window) {
    (void) window;
    output("\033[K");
    return OK;
}

int wrefresh(WINDOW* window) {
    (void) window;
    return 0;
}

int wnoutrefresh(WINDOW* window) {
    return wrefresh(window);
}

int wredrawln(WINDOW* window, int row, int count) {
    for (int i = 0; i < count; ++i) {
        position(window->top + row + i, window->left);
        output("\033[2K");
    }
    return OK;
}

int wscrl(WINDOW* window, int lines) {
    (void) window;
    if (lines > 0)
        output("\033[S");
    else if (lines < 0)
        output("\033[T");
    return OK;
}

int wgetch(WINDOW* window) {
    unsigned char first;
    (void) window;
    if (pushed_key >= 0) {
        int result = pushed_key;
        pushed_key = -1;
        return result;
    }
    if (read_byte(&first) < 0)
        return ERR;
    if (first != 27)
        return first == 127 ? KEY_BACKSPACE : first;
    unsigned char second;
    if (next_escape_byte(&second) < 0)
        return 27;
    if (second != '[' && second != 'O')
        return second == 'b' ? KEY_ALT_L : 27;
    unsigned char code;
    if (read_byte(&code) < 0)
        return 27;
    if (code == 'A')
        return KEY_UP;
    if (code == 'B')
        return KEY_DOWN;
    if (code == 'C')
        return KEY_RIGHT;
    if (code == 'D')
        return KEY_LEFT;
    if (code == 'H')
        return KEY_HOME;
    if (code == 'F')
        return KEY_END;
    if (code == 'Z')
        return KEY_BTAB;
    if (code >= '0' && code <= '9') {
        unsigned char tail;
        if (read_byte(&tail) < 0)
            return 27;
        if (tail == '~') {
            if (code == '2')
                return KEY_IC;
            if (code == '3')
                return KEY_DC;
            if (code == '5')
                return KEY_PPAGE;
            if (code == '6')
                return KEY_NPAGE;
        }
    }
    return 27;
}

int refresh(void) {
    return wrefresh(stdscr);
}
int doupdate(void) {
    return 0;
}
int isendwin(void) {
    return FALSE;
}

int raw(void) {
    struct termios state;
    if (tcgetattr(STDIN_FILENO, &state) < 0)
        return ERR;
    if (!have_saved_termios) {
        saved_termios      = state;
        have_saved_termios = 1;
    }
    state.c_iflag &= ~ICRNL;
    state.c_lflag &= ~(ICANON | ECHO | ISIG);
    state.c_cc[VMIN]  = 1;
    state.c_cc[VTIME] = 0;
    return tcsetattr(STDIN_FILENO, TCSANOW, &state);
}

int noecho(void) {
    return raw();
}
int nonl(void) {
    return OK;
}
int keypad(WINDOW* window, int enabled) {
    (void) window;
    (void) enabled;
    return OK;
}
int nodelay(WINDOW* window, int enabled) {
    (void) window;
    input_nonblocking = enabled != 0;
    return OK;
}
int halfdelay(int tenths) {
    (void) tenths;
    return OK;
}
int scrollok(WINDOW* window, int enabled) {
    (void) window;
    (void) enabled;
    return OK;
}
int curs_set(int visibility) {
    output(visibility ? "\033[?25h" : "\033[?25l");
    return OK;
}
int beep(void) {
    output("\a");
    return OK;
}
int napms(int milliseconds) {
    struct timespec request = {milliseconds / 1000, (milliseconds % 1000) * 1000000L};
    return nanosleep(&request, NULL);
}
int has_colors(void) {
    return FALSE;
}
int start_color(void) {
    return OK;
}
int use_default_colors(void) {
    return OK;
}
int init_pair(short pair, short foreground, short background) {
    (void) pair;
    (void) foreground;
    (void) background;
    return OK;
}
int wattron(WINDOW* window, int attributes) {
    (void) window;
    (void) attributes;
    return OK;
}
int wattroff(WINDOW* window, int attributes) {
    (void) window;
    (void) attributes;
    return OK;
}
int mousemask(mmask_t newmask, mmask_t* oldmask) {
    (void) newmask;
    if (oldmask)
        *oldmask = 0;
    return OK;
}
int mouseinterval(int milliseconds) {
    (void) milliseconds;
    return OK;
}
int wmouse_trafo(const WINDOW* window, int* y, int* x, int to_screen) {
    (void) window;
    (void) y;
    (void) x;
    (void) to_screen;
    return FALSE;
}
int wenclose(const WINDOW* window, int y, int x) {
    return window && y >= window->top && x >= window->left &&
           y < window->top + window->rows && x < window->left + window->cols;
}
int ungetch(int value) {
    if (pushed_key >= 0)
        return ERR;
    pushed_key = value;
    return OK;
}
const char* tigetstr(const char* capability) {
    (void) capability;
    return NULL;
}
int key_defined(const char* sequence) {
    (void) sequence;
    return 0;
}
int define_key(const char* sequence, int keycode) {
    (void) sequence;
    (void) keycode;
    return OK;
}
int typeahead(int file_descriptor) {
    (void) file_descriptor;
    return OK;
}
