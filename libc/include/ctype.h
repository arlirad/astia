#pragma once

/*
 * astia libc — <ctype.h>
 *
 * The "C" locale only: classification is pure ASCII range logic.  Bytes
 * >= 0x80 are not letters, digits, or space; EOF passes through as
 * "none of the above", which is exactly what the standard asks of the C
 * locale.  Function-call style (not macros) so every function has an
 * address and a single evaluation of its argument.
 *
 * No lookup table, deliberately: a static 256-byte table needs
 * initialization, and this libc must work without constructors.
 */

#ifdef __cplusplus
extern "C" {
#endif

int isalnum(int c);
int isalpha(int c);
int isblank(int c);
int iscntrl(int c);
int isdigit(int c);
int isgraph(int c);
int islower(int c);
int isprint(int c);
int ispunct(int c);
int isspace(int c);
int isascii(int c);
int isupper(int c);
int isxdigit(int c);
int tolower(int c);
int toupper(int c);

#ifdef __cplusplus
} /* extern "C" */
#endif
