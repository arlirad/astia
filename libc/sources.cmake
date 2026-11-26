# Single enumeration of the libc translation units.
#
# Grouped by the C/POSIX family the file primarily implements; the directories
# mirror the public headers and keep the build lists from drifting apart.
set(ASTIA_LIBC_CORE_SOURCES
  src/aio/aio.c
  src/aio/backend_aio.c
  src/aio/time_aio.c
  src/aio/time_sleep_aio.c
  src/fs/dirent.c
  src/fs/group.c
  src/fs/path.c
  src/fs/pwd.c
  src/fs/realpath.c
  src/fs/stat.c
  src/io/ioctl.c
  src/io/poll.c
  src/io/pty.c
  src/io/select.c
  src/io/socket.c
  src/io/termcap.c
  src/io/termios.c
  src/io/uio.c
  src/locale/locale.c
  src/math/math.c
  src/process/atfork.c
  src/process/prctl.c
  src/process/process.c
  src/process/rlimit.c
  src/process/semaphore.c
  src/process/setjmp.c
  src/process/signal.c
  src/stdio/file.c
  src/stdio/printf.c
  src/stdio/scanf.c
  src/stdio/snprintf.c
  src/stdlib/assert.c
  src/stdlib/getopt.c
  src/stdlib/malloc.c
  src/stdlib/stdlib.c
  src/string/ctype.c
  src/string/string.c
  src/string/wchar.c
  src/sys/errno.c
  src/sys/mman.c
  src/sys/unistd.c
  src/sys/utsname.c
  src/time/strptime.c
  src/time/time.c
  src/time/times.c
)

# The archive keeps the Astia-side AIO port; the host tests link their own
# backend in front of the archive instead.
set(ASTIA_LIBC_ARCHIVE_EXTRA_SOURCES
  src/aio/process_aio.c
)

# The shared and freestanding userspace variants carry both the pthread
# objects and the Astia-side AIO port.
set(ASTIA_LIBC_SHARED_EXTRA_SOURCES
  src/process/pthread.c
  src/aio/process_aio.c
)

set(ASTIA_LIBC_ALL_SOURCES
  ${ASTIA_LIBC_CORE_SOURCES}
  ${ASTIA_LIBC_ARCHIVE_EXTRA_SOURCES}
  ${ASTIA_LIBC_SHARED_EXTRA_SOURCES}
)
list(REMOVE_DUPLICATES ASTIA_LIBC_ALL_SOURCES)
