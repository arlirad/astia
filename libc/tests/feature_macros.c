#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef _POSIX_C_SOURCE
#ifndef ASTIA_EXPECT_POSIX_C_SOURCE
#error "_POSIX_C_SOURCE must only be visible when the build requested it"
#endif
#if _POSIX_C_SOURCE != ASTIA_EXPECT_POSIX_C_SOURCE
#error "_POSIX_C_SOURCE must retain the requested value"
#endif
#else
#ifdef ASTIA_EXPECT_POSIX_C_SOURCE
#error "_POSIX_C_SOURCE request must remain visible"
#endif
#endif

#ifdef _XOPEN_SOURCE
#ifndef ASTIA_EXPECT_XOPEN_SOURCE
#error "_XOPEN_SOURCE must only be visible when the build requested it"
#endif
#if _XOPEN_SOURCE != ASTIA_EXPECT_XOPEN_SOURCE
#error "_XOPEN_SOURCE must retain the requested value"
#endif
#else
#ifdef ASTIA_EXPECT_XOPEN_SOURCE
#error "_XOPEN_SOURCE request must remain visible"
#endif
#endif

#ifdef _GNU_SOURCE
#ifndef ASTIA_EXPECT_GNU_SOURCE
#error "_GNU_SOURCE must only be visible when the build requested it"
#endif
#if _GNU_SOURCE != ASTIA_EXPECT_GNU_SOURCE
#error "_GNU_SOURCE must retain the requested value"
#endif
#else
#ifdef ASTIA_EXPECT_GNU_SOURCE
#error "_GNU_SOURCE request must remain visible"
#endif
#endif

#ifdef _DEFAULT_SOURCE
#ifndef ASTIA_EXPECT_DEFAULT_SOURCE
#error "_DEFAULT_SOURCE must only be visible when the build requested it"
#endif
#if _DEFAULT_SOURCE != ASTIA_EXPECT_DEFAULT_SOURCE
#error "_DEFAULT_SOURCE must retain the requested value"
#endif
#else
#ifdef ASTIA_EXPECT_DEFAULT_SOURCE
#error "_DEFAULT_SOURCE request must remain visible"
#endif
#endif

#ifdef _FEATURES_H
#error "Astia libc must not provide a feature-test layer"
#endif

#ifdef __USE_POSIX
#error "Astia libc must not derive __USE_POSIX"
#endif
#ifdef __USE_POSIX2
#error "Astia libc must not derive __USE_POSIX2"
#endif
#ifdef __USE_POSIX199309
#error "Astia libc must not derive __USE_POSIX199309"
#endif
#ifdef __USE_POSIX199506
#error "Astia libc must not derive __USE_POSIX199506"
#endif
#ifdef __USE_XOPEN
#error "Astia libc must not derive __USE_XOPEN"
#endif
#ifdef __USE_XOPEN_EXTENDED
#error "Astia libc must not derive __USE_XOPEN_EXTENDED"
#endif
#ifdef __USE_XOPEN2K
#error "Astia libc must not derive __USE_XOPEN2K"
#endif
#ifdef __USE_XOPEN2K8
#error "Astia libc must not derive __USE_XOPEN2K8"
#endif
#ifdef __USE_GNU
#error "Astia libc must not derive __USE_GNU"
#endif
#ifdef __USE_MISC
#error "Astia libc must not derive __USE_MISC"
#endif
#ifdef __USE_FILE_OFFSET64
#error "Astia libc must not derive __USE_FILE_OFFSET64"
#endif
#ifdef __USE_LARGEFILE
#error "Astia libc must not derive __USE_LARGEFILE"
#endif
#ifdef __USE_LARGEFILE64
#error "Astia libc must not derive __USE_LARGEFILE64"
#endif
#ifdef __GLIBC__
#error "Astia libc must not impersonate glibc"
#endif
#ifdef __GLIBC_MINOR__
#error "Astia libc must not impersonate glibc"
#endif
#ifdef __header_inline
#error "Astia libc must not define __header_inline"
#endif
#ifdef __UBCLIBC__
#error "Astia libc must not define __UBCLIBC__"
#endif

int main(void) {
    char* copy       = strdup("astia");
    int   descriptor = fileno(stdin);
    free(copy);
    return descriptor < -1;
}
