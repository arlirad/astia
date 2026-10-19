#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int   tgetent(char* buffer, const char* terminal);
int   tgetflag(const char* capability);
int   tgetnum(const char* capability);
char* tgetstr(const char* capability, char** area);
char* tgoto(const char* capability, int column, int row);
int   tputs(const char* string, int count, int (*putc_fn)(int));

#ifdef __cplusplus
}
#endif
