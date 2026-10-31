#pragma once

/* Wait options.  These values are shared by the direct and async ABIs. */
#define ASTIA_WNOHANG    (1u << 0)
#define ASTIA_WUNTRACED  (1u << 1)
#define ASTIA_WCONTINUED (1u << 3)

/* Traditional wait status layout shared by the kernel and libc. */
#define ASTIA_WAIT_SIGNAL_MASK       0x7f
#define ASTIA_WAIT_CORE_BIT          0x80
#define ASTIA_WAIT_EXIT_MASK         0xff
#define ASTIA_WAIT_STOPPED_MARKER    0x7f
#define ASTIA_WAIT_CONTINUED_MARKER  0xffff
#define ASTIA_WAIT_SIGNAL_EXIT_BASE  128
