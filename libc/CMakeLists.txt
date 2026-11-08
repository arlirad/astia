cmake_minimum_required(VERSION 3.20)

project(astia_libc
  VERSION 0.1.0
  DESCRIPTION "astia C standard library"
  LANGUAGES C)

include(GNUInstallDirs)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(ASTIA_BUILD_TESTS "Build and register host-side unit tests" OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -pedantic)

set(ASTIA_LIBC_SOURCES
  src/assert.c
  src/backend_aio.c
  src/ctype.c
  src/dirent.c
  src/errno.c
  src/file.c
  src/group.c
  src/ioctl.c
  src/locale.c
  src/math.c
  src/malloc.c
  src/mman.c
  src/process.c
  src/pwd.c
  src/path.c
  src/poll.c
  src/select.c
  src/semaphore.c
  src/prctl.c
  src/rlimit.c
  src/printf.c
  src/scanf.c
  src/snprintf.c
  src/signal.c
  src/setjmp.c
  src/stdlib.c
  src/string.c
  src/stat.c
  src/termcap.c
  src/termios.c
  src/time.c
  src/time_aio.c
  src/time_sleep_aio.c
  src/times.c
  src/utsname.c
  src/unistd.c
  src/socket.c
  src/uio.c
  src/wchar.c
)

add_library(astia_libc_objects OBJECT ${ASTIA_LIBC_SOURCES})
add_library(astia_libc STATIC
  $<TARGET_OBJECTS:astia_libc_objects>
  src/process_aio.c
)

# Host tests intentionally use the host pthread implementation as a
# thread source for errno/TLS checks.  The astia pthread object belongs
# to the freestanding userspace image (kernel/tests adds it directly),
# not to those host executables.
if(NOT ASTIA_BUILD_TESTS)
  target_sources(astia_libc_objects PRIVATE src/pthread.c)
endif()

# backend_aio.c is the astia side of the file port: real syscall traps,
# it only ever *executes* on astia.  It is compiled here anyway (and in
# every test build) so the ring ABI cannot rot silently — the host test
# binaries link tests/backend_host.c in front of the archive instead, so
# this member is never pulled into them.

# <errno.h> hands out the userspace ABI's error codes; they live with the
# kernel, this is a header-only dependency.
target_include_directories(astia_libc PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../kernel/include
)
target_include_directories(astia_libc_objects PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../kernel/include
)

# This archive always links *into* an executable (host tests, future
# CRT) — never into a DSO — so nothing needs it exported, and exporting
# hurts: a PIE executable's global symbols do land in .dynsym, so the
# host libc's printf would bind our opaque FILE as its own stdout, and
# its internals could reach our malloc/fwrite with glibc objects.
# Hidden visibility keeps the two libcs in one process strictly
# disjoint; static links inside the executable are unaffected.
target_compile_options(astia_libc PRIVATE -fvisibility=hidden -fno-stack-protector)
target_compile_options(astia_libc_objects PRIVATE -fvisibility=hidden -fno-stack-protector)

if(ASTIA_BUILD_TESTS)
  add_library(astia_libc_host STATIC $<TARGET_OBJECTS:astia_libc_objects>)
  target_include_directories(astia_libc_host PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../kernel/include
  )
endif()

# string.c *defines* the functions the optimizer likes to replace byte
# loops with; -fno-builtin keeps a loop from ever becoming a call to the
# very function being defined.
set_source_files_properties(src/string.c PROPERTIES
  COMPILE_OPTIONS "-fno-builtin"
)

# file.c defines the stdio builtins; GCC's builtin declarations carry
# nonnull attributes, which it merges into our definitions — turning the
# defensive NULL guards (a closed stream's fd == -1 story, setvbuf's
# contract) into -Wnonnull-compare warnings and potentially dead code.
# Not being builtins is exactly the truth of this translation unit.
set_source_files_properties(src/file.c PROPERTIES
  COMPILE_OPTIONS "-fno-builtin"
)
set_source_files_properties(src/printf.c PROPERTIES
  COMPILE_OPTIONS "-fno-builtin"
)
set_source_files_properties(src/time.c PROPERTIES
  COMPILE_OPTIONS "-fno-builtin"
)
set_source_files_properties(src/math.c PROPERTIES
  COMPILE_OPTIONS "-fno-builtin"
)

install(TARGETS astia_libc ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../kernel/include/astia/api/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/astia/api)

if(ASTIA_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
