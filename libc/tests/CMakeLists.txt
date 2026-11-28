# Host-side tests.  Each binary links the astia libc objects in *front
# of the host libc*: our symbols live in the executable, so every call
# from the test (and from the host's own interposable calls) reaches
# shipping astia code, while the host CRT supplies process startup, TLS
# setup, and printf for diagnostics — the few things a libc cannot give
# itself.
find_package(Threads REQUIRED)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/AstiaCTest.cmake)

# Private libc headers (src/internal) are included by name from the tests.
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_library(libc_syscalls_header_alone OBJECT headers/syscalls_header_alone.c)
add_library(libc_syscalls_header_libc OBJECT headers/syscalls_header_libc.c)
target_include_directories(libc_syscalls_header_alone PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../kernel/include
)
target_include_directories(libc_syscalls_header_libc PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../kernel/include
)

add_test(NAME libc_installed_headers
  COMMAND ${CMAKE_COMMAND}
    -DASTIA_CMAKE_COMMAND=${CMAKE_COMMAND}
    -DASTIA_BUILD_DIR=${CMAKE_BINARY_DIR}
    -DASTIA_C_COMPILER=${CMAKE_C_COMPILER}
    -DASTIA_LIBC_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/..
    -DASTIA_HEADER_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/headers/installed_headers.c
    -DASTIA_HEADER_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/installed-headers
    -P ${CMAKE_CURRENT_SOURCE_DIR}/headers/installed_headers_test.cmake)
set_tests_properties(libc_installed_headers
  PROPERTIES LABELS "libc;headers;deterministic")

add_test(NAME libc_feature_macros
  COMMAND ${CMAKE_COMMAND}
    -DASTIA_CMAKE_COMMAND=${CMAKE_COMMAND}
    -DASTIA_BUILD_DIR=${CMAKE_BINARY_DIR}
    -DASTIA_C_COMPILER=${CMAKE_C_COMPILER}
    -DASTIA_LIBC_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/..
    -DASTIA_FEATURE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/headers/feature_macros.c
    -DASTIA_FEATURE_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/feature-macros
    -P ${CMAKE_CURRENT_SOURCE_DIR}/headers/feature_macros_test.cmake)
set_tests_properties(libc_feature_macros
  PROPERTIES LABELS "libc;headers;deterministic" TIMEOUT 120)

# astia_libc deliberately hides its internal port references.  Give the
# host seams the same visibility so the linker resolves those references to
# the test port instead of extracting the real syscall backend from the
# archive (which would both bypass the host fixture and create duplicate
# definitions).
set_source_files_properties(support/backend_host.c support/process_host.c support/socket_backend_host.c
                            support/access_stat_host.c
  PROPERTIES COMPILE_OPTIONS "-fvisibility=hidden")

foreach(_entry string/string string/ctype stdlib/stdlib stdlib/malloc stdio/snprintf)
  get_filename_component(_name ${_entry} NAME)
  add_executable(libc_${_name}_test ${_entry}_test.c)
  target_link_libraries(libc_${_name}_test PRIVATE astia_libc)
  add_test(NAME libc_${_name}_test COMMAND libc_${_name}_test)
endforeach()

add_executable(libc_wchar_test string/wchar_test.c)
target_link_libraries(libc_wchar_test PRIVATE astia_libc)
add_test(NAME libc_wchar_test COMMAND libc_wchar_test)

add_executable(libc_headers_test headers/headers_test.c)
target_link_libraries(libc_headers_test PRIVATE astia_libc)
add_test(NAME libc_headers_test COMMAND libc_headers_test)

add_executable(libc_inet_test net/inet_test.c support/backend_host.c)
target_link_libraries(libc_inet_test PRIVATE astia_libc)
add_test(NAME libc_inet_test COMMAND libc_inet_test)

add_executable(libc_getopt_test stdlib/getopt_test.c support/backend_host.c)
target_link_libraries(libc_getopt_test PRIVATE astia_libc)
add_test(NAME libc_getopt_test COMMAND libc_getopt_test)

add_executable(libc_poll_select_test io/poll_select_test.c support/backend_host.c
  support/socket_backend_host.c)
target_link_libraries(libc_poll_select_test PRIVATE astia_libc)
add_test(NAME libc_poll_select_test COMMAND libc_poll_select_test)

add_executable(libc_posix_file_test integration/posix_file_test.c support/backend_host.c)
target_link_libraries(libc_posix_file_test PRIVATE astia_libc)
add_test(NAME libc_posix_file_test COMMAND libc_posix_file_test)

add_executable(libc_fsync_test fs/fsync_test.c support/backend_host.c)
target_link_libraries(libc_fsync_test PRIVATE astia_libc)
add_test(NAME libc_fsync_test COMMAND libc_fsync_test)

add_executable(libc_termios_test io/termios_test.c support/backend_host.c)
target_link_libraries(libc_termios_test PRIVATE astia_libc)
add_test(NAME libc_termios_test COMMAND libc_termios_test)

add_executable(libc_termios_header_test headers/termios_header_test.c)
target_link_libraries(libc_termios_header_test PRIVATE astia_libc)
add_test(NAME libc_termios_header_test COMMAND libc_termios_header_test)

add_executable(libc_termios_request_test io/termios_request_test.c)
target_link_libraries(libc_termios_request_test PRIVATE astia_libc)
add_test(NAME libc_termios_request_test COMMAND libc_termios_request_test)

add_executable(libc_ttyname_test io/ttyname_test.c support/backend_host.c)
target_link_libraries(libc_ttyname_test PRIVATE astia_libc)
add_test(NAME libc_ttyname_test COMMAND libc_ttyname_test)

add_executable(libc_pty_test io/pty_test.c support/backend_host.c)
target_link_libraries(libc_pty_test PRIVATE astia_libc)
add_test(NAME libc_pty_test COMMAND libc_pty_test)

add_executable(libc_aio_test aio/aio_test.c support/backend_host.c support/time_host.c)
target_link_libraries(libc_aio_test PRIVATE astia_libc)
add_test(NAME libc_aio_test COMMAND libc_aio_test)

add_executable(libc_socket_test io/socket_test.c support/backend_host.c support/socket_backend_host.c)
target_link_libraries(libc_socket_test PRIVATE astia_libc)
add_test(NAME libc_socket_test COMMAND libc_socket_test)

add_executable(libc_sendmsg_test io/sendmsg_test.c support/backend_host.c support/socket_backend_host.c)
target_link_libraries(libc_sendmsg_test PRIVATE astia_libc)
add_test(NAME libc_sendmsg_test COMMAND libc_sendmsg_test)

add_executable(libc_conformance_test integration/conformance_test.c support/backend_host.c support/process_host.c support/time_host.c support/alarm_host.c)
target_link_libraries(libc_conformance_test PRIVATE astia_libc_host)
add_test(NAME libc_conformance_test COMMAND libc_conformance_test)

add_executable(libc_strftime_test time/strftime_test.c support/time_host.c)
target_link_libraries(libc_strftime_test PRIVATE astia_libc)
add_test(NAME libc_strftime_test COMMAND libc_strftime_test)

add_executable(libc_asctime_test time/asctime_test.c support/time_host.c)
target_link_libraries(libc_asctime_test PRIVATE astia_libc)
add_test(NAME libc_asctime_test COMMAND libc_asctime_test)

add_executable(libc_strptime_test time/strptime_test.c support/time_host.c)
target_link_libraries(libc_strptime_test PRIVATE astia_libc)
add_test(NAME libc_strptime_test COMMAND libc_strptime_test)

add_executable(libc_sleep_test time/sleep_test.c support/sleep_host.c support/backend_host.c support/time_host.c)
target_link_libraries(libc_sleep_test PRIVATE astia_libc)
add_test(NAME libc_sleep_test COMMAND libc_sleep_test)

add_executable(libc_alarm_test time/alarm_test.c support/alarm_host.c support/backend_host.c support/process_host.c)
target_link_libraries(libc_alarm_test PRIVATE astia_libc_host)
add_test(NAME libc_alarm_test COMMAND libc_alarm_test)

# The process API drives real host processes: identity, yield and a
# forked child reaped through our waitpid wrapper.  Same object trick
# as the FILE tests — support/process_host.c raw-syscalls the host in front of
# the archive.  support/backend_host.c comes along so the CHECK messages have
# a real write() behind them.
add_executable(libc_process_test process/process_test.c support/process_host.c support/backend_host.c support/alarm_host.c)
target_link_libraries(libc_process_test PRIVATE astia_libc_host)
add_test(NAME libc_process_test COMMAND libc_process_test)

add_executable(libc_atfork_test process/atfork_test.c support/process_host.c support/backend_host.c support/alarm_host.c)
target_link_libraries(libc_atfork_test PRIVATE astia_libc_host)
add_test(NAME libc_atfork_test COMMAND libc_atfork_test)

add_executable(libc_execvp_test process/execvp_test.c support/process_host.c support/backend_host.c support/alarm_host.c)
target_link_libraries(libc_execvp_test PRIVATE astia_libc_host)
add_test(NAME libc_execvp_test COMMAND libc_execvp_test)

add_executable(libc_setjmp_test process/setjmp_test.c)
target_link_libraries(libc_setjmp_test PRIVATE astia_libc)
add_test(NAME libc_setjmp_test COMMAND libc_setjmp_test)

add_executable(libc_pipeline_test integration/pipeline_test.c support/backend_host.c support/process_host.c support/alarm_host.c)
target_link_libraries(libc_pipeline_test PRIVATE astia_libc_host)
add_test(NAME libc_pipeline_test COMMAND libc_pipeline_test)

# The FILE tests drive the whole stream layer against real files: the
# POSIX port (support/backend_host.c) is an *object* of the test binary, which
# puts its __astia_fio_* definitions in front of the archive — the ring
# backend then has nothing left to resolve and stays unlinked.
add_executable(libc_file_test stdio/file_test.c support/backend_host.c)
target_link_libraries(libc_file_test PRIVATE astia_libc)
add_test(NAME libc_file_test COMMAND libc_file_test)

# open_memstream() is the same stream machinery with a memory sink; it
# still links the POSIX port because file.c shares one translation unit,
# but no descriptor call is made by these tests.
add_executable(libc_memstream_test stdio/memstream_test.c support/backend_host.c)
target_link_libraries(libc_memstream_test PRIVATE astia_libc)
add_test(NAME libc_memstream_test COMMAND libc_memstream_test)

add_executable(libc_scanf_test stdio/scanf_test.c)
target_link_libraries(libc_scanf_test PRIVATE astia_libc)
add_test(NAME libc_scanf_test COMMAND libc_scanf_test)

# <sys/uio.h>: the position-based vector pair, same object trick.  The
# port's readv/writev go by raw syscall there, because this libc is the
# definition the binary would otherwise resolve them to.
add_executable(libc_uio_test io/uio_test.c support/backend_host.c)
target_link_libraries(libc_uio_test PRIVATE astia_libc)
add_test(NAME libc_uio_test COMMAND libc_uio_test)

add_executable(libc_stat_test fs/stat_test.c support/backend_host.c support/process_host.c support/alarm_host.c)
target_link_libraries(libc_stat_test PRIVATE astia_libc_host)
add_test(NAME libc_stat_test COMMAND libc_stat_test)

add_executable(libc_access_test fs/access_test.c support/access_stat_host.c support/backend_host.c
                                  support/process_host.c support/alarm_host.c)
target_link_libraries(libc_access_test PRIVATE astia_libc_host)
add_test(NAME libc_access_test COMMAND libc_access_test)

add_executable(libc_rename_test fs/rename_test.c support/backend_host.c)
target_link_libraries(libc_rename_test PRIVATE astia_libc)
add_test(NAME libc_rename_test COMMAND libc_rename_test)

add_executable(libc_realpath_test fs/realpath_test.c support/backend_host.c)
target_link_libraries(libc_realpath_test PRIVATE astia_libc)
add_test(NAME libc_realpath_test COMMAND libc_realpath_test)

add_executable(libc_creat_test fs/creat_test.c support/backend_host.c)
target_link_libraries(libc_creat_test PRIVATE astia_libc)
add_test(NAME libc_creat_test COMMAND libc_creat_test)

add_executable(libc_settimeofday_test time/settimeofday_test.c support/time_host.c)
target_link_libraries(libc_settimeofday_test PRIVATE astia_libc)
add_test(NAME libc_settimeofday_test COMMAND libc_settimeofday_test)

add_executable(libc_dirent_test fs/dirent_test.c support/backend_host.c)
target_link_libraries(libc_dirent_test PRIVATE astia_libc)
add_test(NAME libc_dirent_test COMMAND libc_dirent_test)

add_executable(libc_mktemp_test stdlib/mktemp_test.c support/backend_host.c)
target_link_libraries(libc_mktemp_test PRIVATE astia_libc)
add_test(NAME libc_mktemp_test COMMAND libc_mktemp_test)

add_executable(libc_descriptor_test io/descriptor_test.c support/backend_host.c)
target_link_libraries(libc_descriptor_test PRIVATE astia_libc)
add_test(NAME libc_descriptor_test COMMAND libc_descriptor_test)

add_executable(libc_lseek_test fs/lseek_test.c support/backend_host.c)
target_link_libraries(libc_lseek_test PRIVATE astia_libc)
add_test(NAME libc_lseek_test COMMAND libc_lseek_test)

add_executable(libc_utsname_test sys/utsname_test.c support/process_host.c support/backend_host.c support/alarm_host.c)
target_link_libraries(libc_utsname_test PRIVATE astia_libc_host)
add_test(NAME libc_utsname_test COMMAND libc_utsname_test)

add_executable(libc_group_test fs/group_test.c support/process_host.c support/backend_host.c support/alarm_host.c)
target_link_libraries(libc_group_test PRIVATE astia_libc_host)
add_test(NAME libc_group_test COMMAND libc_group_test)

add_executable(libc_pwd_test fs/pwd_test.c support/process_host.c support/backend_host.c support/alarm_host.c)
target_link_libraries(libc_pwd_test PRIVATE astia_libc_host)
add_test(NAME libc_pwd_test COMMAND libc_pwd_test)

add_executable(libc_gethostname_test sys/gethostname_test.c support/backend_host.c)
target_link_libraries(libc_gethostname_test PRIVATE astia_libc)
add_test(NAME libc_gethostname_test COMMAND libc_gethostname_test)

# errno's thread-locality is a design contract, so it gets a real second
# thread: host pthreads as a thread source only, the object under test
# is ours.
add_executable(libc_errno_test sys/errno_test.c)
target_link_libraries(libc_errno_test PRIVATE astia_libc Threads::Threads)
add_test(NAME libc_errno_test COMMAND libc_errno_test)

add_executable(libc_perror_test stdio/perror_test.c support/backend_host.c)
target_link_libraries(libc_perror_test PRIVATE astia_libc)
add_test(NAME libc_perror_test COMMAND libc_perror_test)

add_executable(libc_syslog_test sys/syslog_test.c support/backend_host.c
                                support/process_host.c support/alarm_host.c)
target_link_libraries(libc_syslog_test PRIVATE astia_libc_host Threads::Threads)
add_test(NAME libc_syslog_test COMMAND libc_syslog_test)

add_executable(libc_sigwait_test process/sigwait_test.c support/backend_host.c support/process_host.c support/alarm_host.c)
target_link_libraries(libc_sigwait_test PRIVATE astia_libc_host)
add_test(NAME libc_sigwait_test COMMAND libc_sigwait_test)

add_executable(libc_sigtimedwait_test process/sigtimedwait_test.c)
target_link_libraries(libc_sigtimedwait_test PRIVATE astia_libc)
add_test(NAME libc_sigtimedwait_test COMMAND libc_sigtimedwait_test)

add_executable(libc_process_group_test process/process_group_test.c support/process_host.c support/backend_host.c support/alarm_host.c)
target_link_libraries(libc_process_group_test PRIVATE astia_libc_host)
add_test(NAME libc_process_group_test COMMAND libc_process_group_test)

add_executable(libc_chroot_test process/chroot_test.c support/process_host.c support/backend_host.c support/alarm_host.c)
target_link_libraries(libc_chroot_test PRIVATE astia_libc_host)
add_test(NAME libc_chroot_test COMMAND libc_chroot_test)

add_executable(libc_killpg_test process/killpg_test.c support/process_host.c support/backend_host.c support/alarm_host.c)
target_link_libraries(libc_killpg_test PRIVATE astia_libc_host)
add_test(NAME libc_killpg_test COMMAND libc_killpg_test)

add_executable(libc_signal_api_test process/signal_api_test.c)
target_link_libraries(libc_signal_api_test PRIVATE astia_libc)
add_test(NAME libc_signal_api_test COMMAND libc_signal_api_test)

add_executable(libc_semaphore_test process/semaphore_test.c support/semaphore_host.c support/backend_host.c support/process_host.c support/alarm_host.c support/time_host.c)
target_link_libraries(libc_semaphore_test PRIVATE astia_libc_host)
add_test(NAME libc_semaphore_test COMMAND libc_semaphore_test)

# pthread_once is the one pthread entry with no host stand-in: the shipping
# translation unit is compiled straight into the test, so the once protocol
# runs over a real futex and host pthreads are only the thread source.
add_executable(libc_pthread_once_test process/pthread_once_test.c
  support/futex_host.c support/process_host.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/process/pthread_once.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/sys/errno.c)
target_include_directories(libc_pthread_once_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
  ${CMAKE_CURRENT_SOURCE_DIR}/../src
  ${CMAKE_CURRENT_SOURCE_DIR}/../../kernel/include)
target_link_libraries(libc_pthread_once_test PRIVATE Threads::Threads)
add_test(NAME libc_pthread_once_test COMMAND libc_pthread_once_test)

astia_append_test_label("libc;host;deterministic")
astia_append_test_label("sanitizer-unsupported-libc")
