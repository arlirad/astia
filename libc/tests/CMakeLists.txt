# Host-side tests.  Each binary links the astia libc objects in *front
# of the host libc*: our symbols live in the executable, so every call
# from the test (and from the host's own interposable calls) reaches
# shipping astia code, while the host CRT supplies process startup, TLS
# setup, and printf for diagnostics — the few things a libc cannot give
# itself.
find_package(Threads REQUIRED)

add_library(libc_syscalls_header_alone OBJECT syscalls_header_alone.c)
add_library(libc_syscalls_header_libc OBJECT syscalls_header_libc.c)
target_include_directories(libc_syscalls_header_alone PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../kernel/include
)
target_include_directories(libc_syscalls_header_libc PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../kernel/include
)

# astia_libc deliberately hides its internal port references.  Give the
# host seams the same visibility so the linker resolves those references to
# the test port instead of extracting the real syscall backend from the
# archive (which would both bypass the host fixture and create duplicate
# definitions).
set_source_files_properties(backend_host.c process_host.c
  PROPERTIES COMPILE_OPTIONS "-fvisibility=hidden")

foreach(_t string ctype stdlib malloc snprintf)
  add_executable(libc_${_t}_test ${_t}_test.c)
  target_link_libraries(libc_${_t}_test PRIVATE astia_libc)
  add_test(NAME libc_${_t}_test COMMAND libc_${_t}_test)
endforeach()

add_executable(libc_posix_file_test posix_file_test.c backend_host.c)
target_link_libraries(libc_posix_file_test PRIVATE astia_libc)
add_test(NAME libc_posix_file_test COMMAND libc_posix_file_test)

add_executable(libc_aio_test aio_test.c backend_host.c time_host.c)
target_link_libraries(libc_aio_test PRIVATE astia_libc)
add_test(NAME libc_aio_test COMMAND libc_aio_test)

add_executable(libc_conformance_test conformance_test.c backend_host.c process_host.c time_host.c alarm_host.c)
target_link_libraries(libc_conformance_test PRIVATE astia_libc_host)
add_test(NAME libc_conformance_test COMMAND libc_conformance_test)

add_executable(libc_sleep_test sleep_test.c sleep_host.c backend_host.c time_host.c)
target_link_libraries(libc_sleep_test PRIVATE astia_libc)
add_test(NAME libc_sleep_test COMMAND libc_sleep_test)

add_executable(libc_alarm_test alarm_test.c alarm_host.c backend_host.c process_host.c)
target_link_libraries(libc_alarm_test PRIVATE astia_libc_host)
add_test(NAME libc_alarm_test COMMAND libc_alarm_test)

# The process API drives real host processes: identity, yield and a
# forked child reaped through our waitpid wrapper.  Same object trick
# as the FILE tests — process_host.c raw-syscalls the host in front of
# the archive.  backend_host.c comes along so the CHECK messages have
# a real write() behind them.
add_executable(libc_process_test process_test.c process_host.c backend_host.c alarm_host.c)
target_link_libraries(libc_process_test PRIVATE astia_libc_host)
add_test(NAME libc_process_test COMMAND libc_process_test)

add_executable(libc_atfork_test atfork_test.c process_host.c backend_host.c alarm_host.c)
target_link_libraries(libc_atfork_test PRIVATE astia_libc_host)
add_test(NAME libc_atfork_test COMMAND libc_atfork_test)

add_executable(libc_pipeline_test pipeline_test.c backend_host.c process_host.c alarm_host.c)
target_link_libraries(libc_pipeline_test PRIVATE astia_libc_host)
add_test(NAME libc_pipeline_test COMMAND libc_pipeline_test)

# The FILE tests drive the whole stream layer against real files: the
# POSIX port (backend_host.c) is an *object* of the test binary, which
# puts its __astia_fio_* definitions in front of the archive — the ring
# backend then has nothing left to resolve and stays unlinked.
add_executable(libc_file_test file_test.c backend_host.c)
target_link_libraries(libc_file_test PRIVATE astia_libc)
add_test(NAME libc_file_test COMMAND libc_file_test)

# <sys/uio.h>: the position-based vector pair, same object trick.  The
# port's readv/writev go by raw syscall there, because this libc is the
# definition the binary would otherwise resolve them to.
add_executable(libc_uio_test uio_test.c backend_host.c)
target_link_libraries(libc_uio_test PRIVATE astia_libc)
add_test(NAME libc_uio_test COMMAND libc_uio_test)

add_executable(libc_stat_test stat_test.c backend_host.c)
target_link_libraries(libc_stat_test PRIVATE astia_libc)
add_test(NAME libc_stat_test COMMAND libc_stat_test)

add_executable(libc_rename_test rename_test.c backend_host.c)
target_link_libraries(libc_rename_test PRIVATE astia_libc)
add_test(NAME libc_rename_test COMMAND libc_rename_test)

add_executable(libc_descriptor_test descriptor_test.c backend_host.c)
target_link_libraries(libc_descriptor_test PRIVATE astia_libc)
add_test(NAME libc_descriptor_test COMMAND libc_descriptor_test)

# errno's thread-locality is a design contract, so it gets a real second
# thread: host pthreads as a thread source only, the object under test
# is ours.
add_executable(libc_errno_test errno_test.c)
target_link_libraries(libc_errno_test PRIVATE astia_libc Threads::Threads)
add_test(NAME libc_errno_test COMMAND libc_errno_test)

add_executable(libc_perror_test perror_test.c backend_host.c)
target_link_libraries(libc_perror_test PRIVATE astia_libc)
add_test(NAME libc_perror_test COMMAND libc_perror_test)

add_executable(libc_sigwait_test sigwait_test.c backend_host.c process_host.c alarm_host.c)
target_link_libraries(libc_sigwait_test PRIVATE astia_libc_host)
add_test(NAME libc_sigwait_test COMMAND libc_sigwait_test)

add_executable(libc_sigtimedwait_test sigtimedwait_test.c)
target_link_libraries(libc_sigtimedwait_test PRIVATE astia_libc)
add_test(NAME libc_sigtimedwait_test COMMAND libc_sigtimedwait_test)

add_executable(libc_process_group_test process_group_test.c process_host.c backend_host.c alarm_host.c)
target_link_libraries(libc_process_group_test PRIVATE astia_libc_host)
add_test(NAME libc_process_group_test COMMAND libc_process_group_test)

add_executable(libc_signal_api_test signal_api_test.c)
target_link_libraries(libc_signal_api_test PRIVATE astia_libc)
add_test(NAME libc_signal_api_test COMMAND libc_signal_api_test)

add_executable(libc_semaphore_test semaphore_test.c semaphore_host.c backend_host.c process_host.c alarm_host.c time_host.c)
target_link_libraries(libc_semaphore_test PRIVATE astia_libc_host)
add_test(NAME libc_semaphore_test COMMAND libc_semaphore_test)
