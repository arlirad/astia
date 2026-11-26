cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED ASTIA_VERSION)
  include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/AstiaVersion.cmake)
endif()

project(astia_libc
  VERSION ${ASTIA_VERSION_major}.${ASTIA_VERSION_minor}.${ASTIA_VERSION_patch}
  DESCRIPTION "astia C standard library"
  LANGUAGES C)

include_directories(${ASTIA_VERSION_INCLUDE_DIR})

include(GNUInstallDirs)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(ASTIA_BUILD_TESTS "Build and register host-side unit tests" OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -pedantic)

include(${CMAKE_CURRENT_SOURCE_DIR}/sources.cmake)

add_library(astia_libc_objects OBJECT ${ASTIA_LIBC_CORE_SOURCES})
add_library(astia_libc STATIC
  $<TARGET_OBJECTS:astia_libc_objects>
  ${ASTIA_LIBC_ARCHIVE_EXTRA_SOURCES}
)

# libc.so for dynamically linked userspace images.  It is compiled separately
# from the archive: PIC and initial-exec TLS are mandatory for a DSO, while the
# archive stays non-PIC so it can link into the fixed-address executables
# (zsh, coreutils).  Visibility stays default here -- the archive hides its
# symbols, but a shared library must export them for the interpreter.
# ASTIA_LIBC_SHARED_BUILTINS lets a cross build point at libclang_rt.builtins.
set(ASTIA_LIBC_SHARED_BUILTINS "" CACHE FILEPATH
  "clang runtime builtins archive to link into libc.so")
add_library(astia_libc_shared_objects STATIC
  ${ASTIA_LIBC_CORE_SOURCES}
  ${ASTIA_LIBC_SHARED_EXTRA_SOURCES}
)
target_include_directories(astia_libc_shared_objects PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../kernel/include
)
target_compile_options(astia_libc_shared_objects PRIVATE
  -fPIC -ftls-model=initial-exec -fno-stack-protector)
set_target_properties(astia_libc_shared_objects PROPERTIES
  POSITION_INDEPENDENT_CODE ON)

# The bare-metal driver for x86_64-unknown-elf drops -shared (and -pie), so
# CMake's SHARED rule would silently run llvm-ar.  Link the DSO explicitly
# with the linker-level flag, which both GNU ld and lld accept.
set(ASTIA_LIBC_SHARED_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/libc.so)
add_custom_command(
  OUTPUT ${ASTIA_LIBC_SHARED_OUTPUT}
  COMMAND ${CMAKE_C_COMPILER} -nostdlib -Wl,-shared -Wl,-soname,libc.so
          -Wl,-z,now -o ${ASTIA_LIBC_SHARED_OUTPUT}
          -Wl,--whole-archive $<TARGET_FILE:astia_libc_shared_objects>
          -Wl,--no-whole-archive ${ASTIA_LIBC_SHARED_BUILTINS}
  DEPENDS $<TARGET_FILE:astia_libc_shared_objects>
  COMMAND_EXPAND_LISTS
  COMMENT "Linking libc.so")
add_custom_target(astia_libc_shared ALL DEPENDS ${ASTIA_LIBC_SHARED_OUTPUT})

# Host tests intentionally use the host pthread implementation as a
# thread source for errno/TLS checks.  The astia pthread object belongs
# to the freestanding userspace image (kernel/tests adds it directly),
# not to those host executables.
if(NOT ASTIA_BUILD_TESTS)
  target_sources(astia_libc_objects PRIVATE src/process/pthread.c)
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
target_include_directories(astia_libc PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_include_directories(astia_libc_objects PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
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
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../kernel/include
  )
endif()

# string.c *defines* the functions the optimizer likes to replace byte
# loops with; -fno-builtin keeps a loop from ever becoming a call to the
# very function being defined.
set_source_files_properties(src/string/string.c PROPERTIES
  COMPILE_OPTIONS "-fno-builtin"
)

# file.c defines the stdio builtins; GCC's builtin declarations carry
# nonnull attributes, which it merges into our definitions — turning the
# defensive NULL guards (a closed stream's fd == -1 story, setvbuf's
# contract) into -Wnonnull-compare warnings and potentially dead code.
# Not being builtins is exactly the truth of this translation unit.
set_source_files_properties(src/stdio/file.c PROPERTIES
  COMPILE_OPTIONS "-fno-builtin"
)
set_source_files_properties(src/stdio/printf.c PROPERTIES
  COMPILE_OPTIONS "-fno-builtin"
)
set_source_files_properties(src/time/time.c PROPERTIES
  COMPILE_OPTIONS "-fno-builtin"
)
set_source_files_properties(src/math/math.c PROPERTIES
  COMPILE_OPTIONS "-fno-builtin"
)
set_source_files_properties(src/string/wchar.c PROPERTIES
  COMPILE_OPTIONS "-fno-builtin"
)

install(TARGETS astia_libc ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES ${ASTIA_LIBC_SHARED_OUTPUT} DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES
  ${ASTIA_VERSION_INCLUDE_DIR}/astia/version.h
  ${ASTIA_VERSION_INCLUDE_DIR}/astia/version.hpp
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/astia
)

set(ASTIA_LIBC_KERNEL_HEADERS
  ${CMAKE_CURRENT_SOURCE_DIR}/../kernel/include/astia/api/aio.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../kernel/include/astia/api/errno.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../kernel/include/astia/api/signal.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../kernel/include/astia/api/signal_stack.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../kernel/include/astia/api/syscalls.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../kernel/include/astia/api/termios.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../kernel/include/astia/api/wait.h
)
install(FILES
  ${ASTIA_LIBC_KERNEL_HEADERS}
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/astia/api
)

if(ASTIA_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
