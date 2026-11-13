#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* Every implementation-defined byte loop lives in string.c, which is
   compiled with -fno-builtin (see CMakeLists.txt) so the compiler can
   never rewrite one of these loops into a call to the very function
   being defined. */

void* memcpy(void* dst, const void* src, size_t n) {
    unsigned char*       d = dst;
    const unsigned char* s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}

void* memmove(void* dst, const void* src, size_t n) {
    unsigned char*       d = dst;
    const unsigned char* s = src;
    if (d == s || n == 0)
        return dst;
    if (d < s) {
        while (n--)
            *d++ = *s++;
    }
    else {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dst;
}

void* memset(void* dst, int c, size_t n) {
    unsigned char* d = dst;
    while (n--)
        *d++ = (unsigned char) c;
    return dst;
}

int memcmp(const void* a, const void* b, size_t n) {
    const unsigned char* x = a;
    const unsigned char* y = b;
    while (n--) {
        if (*x != *y)
            return (int) *x - (int) *y;
        x++;
        y++;
    }
    return 0;
}

void* memchr(const void* s, int c, size_t n) {
    const unsigned char* p = s;
    unsigned char        t = (unsigned char) c;
    for (; n--; p++)
        if (*p == t)
            return (void*) p;
    return NULL;
}

size_t strlen(const char* s) {
    const char* p = s;
    while (*p)
        p++;
    return (size_t) (p - s);
}

size_t strnlen(const char* s, size_t n) {
    size_t i = 0;
    while (i < n && s[i])
        i++;
    return i;
}

int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char) *a - (unsigned char) *b;
}

int strncmp(const char* a, const char* b, size_t n) {
    if (n == 0)
        return 0;
    while (--n && *a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char) *a - (unsigned char) *b;
}

int strcoll(const char* a, const char* b) {
    return strcmp(a, b);
}

size_t strxfrm(char* dst, const char* src, size_t n) {
    size_t len = strlen(src);
    if (n != 0) {
        size_t copy = len < n - 1 ? len : n - 1;
        memcpy(dst, src, copy);
        dst[copy] = '\0';
    }
    return len;
}

char* strcpy(char* dst, const char* src) {
    char* r = dst;
    while ((*dst++ = *src++) != '\0')
        ;
    return r;
}

char* strncpy(char* dst, const char* src, size_t n) {
    char* r = dst;
    while (n && *src) {
        *dst++ = *src++;
        n--;
    }
    while (n--)
        *dst++ = '\0';
    return r;
}

char* strcat(char* dst, const char* src) {
    char* r = dst;
    while (*dst)
        dst++;
    strcpy(dst, src);
    return r;
}

char* strncat(char* dst, const char* src, size_t limit) {
    size_t offset = strlen(dst);
    size_t index  = 0;
    while (index < limit && src[index] != '\0') {
        dst[offset + index] = src[index];
        ++index;
    }
    dst[offset + index] = '\0';
    return dst;
}

char* strchr(const char* s, int c) {
    char t = (char) c;
    for (;; s++) {
        if (*s == t)
            return (char*) s;
        if (*s == '\0')
            return NULL;
    }
}

char* strrchr(const char* s, int c) {
    char        t    = (char) c;
    const char* last = NULL;
    do {
        if (*s == t)
            last = s;
    } while (*s++);
    return (char*) last;
}

char* strstr(const char* hay, const char* needle) {
    if (*needle == '\0')
        return (char*) hay;
    for (; *hay; hay++) {
        const char* a = hay;
        const char* b = needle;
        while (*a && *b && *a == *b) {
            a++;
            b++;
        }
        if (*b == '\0')
            return (char*) hay;
    }
    return NULL;
}

size_t strspn(const char* s, const char* accept) {
    size_t i = 0;
    while (s[i] && strchr(accept, s[i]))
        i++;
    return i;
}

size_t strcspn(const char* s, const char* reject) {
    size_t i = 0;
    while (s[i] && !strchr(reject, s[i]))
        i++;
    return i;
}

char* strpbrk(const char* s, const char* accept) {
    while (*s) {
        if (strchr(accept, *s))
            return (char*) s;
        s++;
    }
    return NULL;
}

char* strtok(char* s, const char* delim) {
    /* The state has to live somewhere: the API reaches it through a NULL
       argument.  Thread-local (the glibc answer) — two threads tokenizing
       their own buffers do not clobber each other; only a *single*
       sequence shared across threads is still ambiguous, which is why
       strtok_r arrives with the thread runtime. */
    static _Thread_local char* next;

    if (s == NULL)
        s = next;
    if (s == NULL)
        return NULL;
    s += strspn(s, delim); /* skip leading delimiters */
    if (*s == '\0') {
        next = NULL; /* exhausted the tail */
        return NULL;
    }
    char* end = s + strcspn(s, delim);
    if (*end != '\0') {
        *end = '\0';
        next = end + 1;
    }
    else {
        next = NULL;
    }
    return s;
}

char* strdup(const char* s) {
    size_t len;
    char*  out;

    if (s == NULL)
        return NULL;
    len = strlen(s);
    if (len == (size_t) -1) {
        errno = ENOMEM;
        return NULL;
    }
    out = malloc(len + 1);
    if (out == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    memcpy(out, s, len + 1);
    return out;
}

char* strndup(const char* s, size_t n) {
    size_t len;
    char*  out;

    if (s == NULL)
        return NULL;
    len = strnlen(s, n);
    if (len == (size_t) -1) {
        errno = ENOMEM;
        return NULL;
    }
    out = malloc(len + 1);
    if (out == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

int strcasecmp(const char* a, const char* b) {
    while (*a != '\0' && *b != '\0') {
        int ca = tolower((unsigned char) *a);
        int cb = tolower((unsigned char) *b);
        if (ca != cb)
            return ca < cb ? -1 : 1;
        a++;
        b++;
    }
    return *a == *b ? 0 : (*a == '\0' ? -1 : 1);
}

int strncasecmp(const char* a, const char* b, size_t n) {
    while (n != 0 && *a != '\0' && *b != '\0') {
        int ca = tolower((unsigned char) *a);
        int cb = tolower((unsigned char) *b);
        if (ca != cb)
            return ca < cb ? -1 : 1;
        a++;
        b++;
        n--;
    }
    if (n == 0)
        return 0;
    return *a == *b ? 0 : (*a == '\0' ? -1 : 1);
}

int bcmp(const void* left, const void* right, size_t length) {
    return memcmp(left, right, length);
}

void bcopy(const void* source, void* destination, size_t length) {
    (void) memmove(destination, source, length);
}

void bzero(void* destination, size_t length) {
    (void) memset(destination, 0, length);
}

int ffs(int value) {
    unsigned int bits = (unsigned int) value;
    int          bit  = 1;
    if (bits == 0)
        return 0;
    while ((bits & 1u) == 0) {
        bits >>= 1;
        ++bit;
    }
    return bit;
}

char* index(const char* string, int character) {
    return strchr(string, character);
}

char* rindex(const char* string, int character) {
    return strrchr(string, character);
}

char* strerror(int errnum) {
    switch (errnum) {
    case EPERM:
        return "Operation not permitted";
    case ENOENT:
        return "No such file or directory";
    case ESRCH:
        return "No such process";
    case EINTR:
        return "Interrupted function call";
    case EIO:
        return "Input/output error";
    case EBADF:
        return "Bad file descriptor";
    case ECHILD:
        return "No child processes";
    case EAGAIN:
        return "Resource temporarily unavailable";
    case ENOMEM:
        return "Cannot allocate memory";
    case EACCES:
        return "Permission denied";
    case EEXIST:
        return "File exists";
    case ENOTDIR:
        return "Not a directory";
    case EISDIR:
        return "Is a directory";
    case ENOTTY:
        return "Inappropriate ioctl for device";
    case EINVAL:
        return "Invalid argument";
    case EMFILE:
        return "Too many open files";
    case ENOSPC:
        return "No space left on device";
    case EPIPE:
        return "Broken pipe";
    case EDOM:
        return "Numerical argument out of domain";
    case ERANGE:
        return "Numerical result out of range";
    case ENOSYS:
        return "Function not implemented";
    case ETIMEDOUT:
        return "Connection timed out";
    case ECANCELED:
        return "Operation canceled";
    case EILSEQ:
        return "Illegal byte sequence";
    default:
        return "Unknown error";
    }
}
