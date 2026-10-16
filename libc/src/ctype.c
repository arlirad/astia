#include <ctype.h>

/*
 * Range-logic implementations: no table (tables need initialization and
 * this libc runs without constructors), correct for every valid argument
 * including EOF, which falls outside every range and therefore classifies
 * as "not whatever" — the C locale answer.
 */

int isdigit(int c) {
    return c >= '0' && c <= '9';
}

int isalpha(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

int isalnum(int c) {
    return isalpha(c) || isdigit(c);
}

int isspace(int c) {
    return c == ' ' || (c >= '\t' && c <= '\r'); /* \t \n \v \f \r */
}

int isblank(int c) {
    return c == ' ' || c == '\t';
}

int isupper(int c) {
    return c >= 'A' && c <= 'Z';
}

int islower(int c) {
    return c >= 'a' && c <= 'z';
}

int isxdigit(int c) {
    return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int iscntrl(int c) {
    return (c >= 0 && c < 0x20) || c == 0x7f;
}

int isprint(int c) {
    return c >= 0x20 && c < 0x7f;
}

int isgraph(int c) {
    return c > 0x20 && c < 0x7f;
}

int ispunct(int c) {
    return isgraph(c) && !isalnum(c);
}

int tolower(int c) {
    return isupper(c) ? c + ('a' - 'A') : c;
}

int toupper(int c) {
    return islower(c) ? c - ('a' - 'A') : c;
}
