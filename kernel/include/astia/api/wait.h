#pragma once

/* Traditional wait status layout shared by the kernel and libc. */
#define ASTIA_WAIT_SIGNAL_MASK       0x7f
#define ASTIA_WAIT_CORE_BIT          0x80
#define ASTIA_WAIT_EXIT_MASK         0xff
#define ASTIA_WAIT_STOPPED_MARKER    0x7f
#define ASTIA_WAIT_CONTINUED_MARKER  0xffff
#define ASTIA_WAIT_SIGNAL_EXIT_BASE  128
