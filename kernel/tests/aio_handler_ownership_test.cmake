# Script mode (-P) does not inherit a project's CMP0057 policy, so IN_LIST
# would otherwise be rejected by CMake versions that keep the policy OLD.
if (POLICY CMP0057)
    cmake_policy(SET CMP0057 NEW)
endif ()

if (NOT DEFINED ASTIA_KERNEL_SOURCE_DIR)
    message(FATAL_ERROR "ASTIA_KERNEL_SOURCE_DIR is required")
endif ()

set(_src "${ASTIA_KERNEL_SOURCE_DIR}/src")
set(_aio "${_src}/io/aio")

if (EXISTS "${_aio}/operations")
    message(FATAL_ERROR "concrete AIO handlers must not live under ${_aio}/operations")
endif ()
if (EXISTS "${_aio}/operation_registration.hpp")
    message(FATAL_ERROR
            "AIO registration header must not exist: ${_aio}/operation_registration.hpp")
endif ()

foreach (_old_file
        "${_src}/fs/aio_handlers.cpp"
        "${_src}/net/aio_handlers.cpp"
        "${_src}/io/readiness_handlers.cpp"
        "${_aio}/core_handlers.cpp")
    if (EXISTS "${_old_file}")
        message(FATAL_ERROR "central aggregate handler file remains: ${_old_file}")
    endif ()
endforeach ()

foreach (_retired_dispatch
        "${_src}/fs/aio/dispatch.cpp"
        "${_src}/net/aio/dispatch.cpp"
        "${_src}/io/readiness/dispatch.cpp"
        "${_src}/fs/aio/dispatch.hpp"
        "${_src}/net/aio/dispatch.hpp"
        "${_src}/io/readiness/dispatch.hpp")
    if (EXISTS "${_retired_dispatch}")
        message(FATAL_ERROR
                "aggregate handler dispatch must not remain: ${_retired_dispatch}")
    endif ()
endforeach ()

set(_enosys_handlers spawn wait)

function(astia_aio_check_handler _file _stem)
    file(READ "${_file}" _contents)
    string(FIND "${_contents}" "ASTIA_AIO_HOST_TEST" _host_offset)
    if (NOT _host_offset EQUAL -1)
        message(FATAL_ERROR "${_file}: host-only AIO handler bypass remains")
    endif ()
    if (_contents MATCHES
            "filesystem::dispatch|socket::dispatch|readiness::dispatch|dispatch::dispatch_core")
        message(FATAL_ERROR "${_file}: production handler is a trivial forwarder")
    endif ()
    if (_contents MATCHES "bool handle_[a-z0-9_]+[ \t]*\\("
            OR _contents MATCHES "return handle_[a-z0-9_]+[ \t]*\\(")
        message(FATAL_ERROR "${_file}: owner handler must keep its body in invoke")
    endif ()
    if (NOT _contents MATCHES "return ")
        message(FATAL_ERROR "${_file}: production path has no implementation body")
    endif ()
    if (_stem IN_LIST _enosys_handlers AND NOT _contents MATCHES "-ENOSYS")
        message(FATAL_ERROR "${_file}: intentional ENOSYS handler lost its contract")
    endif ()
endfunction()

set(_owners
        "${_aio}/core|15"
        "${_src}/fs/aio|15"
        "${_src}/net/aio|11"
        "${_src}/io/readiness/aio|2")

foreach (_entry IN LISTS _owners)
    string(REPLACE "|" ";" _parts "${_entry}")
    list(GET _parts 0 _dir)
    list(GET _parts 1 _expected)
    if (NOT IS_DIRECTORY "${_dir}")
        message(FATAL_ERROR "missing owner handler directory: ${_dir}")
    endif ()
    file(GLOB _files "${_dir}/*.cpp")
    list(REMOVE_ITEM _files "${_dir}/dispatch.cpp")
    list(LENGTH _files _file_count)
    if (NOT _file_count EQUAL _expected)
        message(FATAL_ERROR
                "${_dir}: expected ${_expected} handler files, found ${_file_count}")
    endif ()
    set(_total 0)
    foreach (_file IN LISTS _files)
        file(READ "${_file}" _contents)
        get_filename_component(_stem "${_file}" NAME_WE)
        string(REGEX MATCHALL "_descriptor =" _matches "${_contents}")
        list(LENGTH _matches _count)
        if (NOT _count EQUAL 1)
            message(FATAL_ERROR
                    "${_file}: expected exactly one descriptor, found ${_count}")
        endif ()
        string(FIND "${_contents}"
                "extern const operation_descriptor ${_stem}_descriptor" _named)
        if (_named EQUAL -1)
            message(FATAL_ERROR
                    "${_file}: descriptor does not match handler file name ${_stem}")
        endif ()
        astia_aio_check_handler("${_file}" "${_stem}")
        math(EXPR _total "${_total} + ${_count}")
    endforeach ()
    if (NOT _total EQUAL _expected)
        message(FATAL_ERROR
                "${_dir}: expected ${_expected} descriptors, found ${_total}")
    endif ()
endforeach ()

file(GLOB_RECURSE _scan
        "${_aio}/*.c"
        "${_aio}/*.cpp"
        "${_aio}/*.h"
        "${_aio}/*.hpp"
        "${_src}/fs/aio/*.c"
        "${_src}/fs/aio/*.cpp"
        "${_src}/fs/aio/*.h"
        "${_src}/fs/aio/*.hpp"
        "${_src}/net/aio/*.c"
        "${_src}/net/aio/*.cpp"
        "${_src}/net/aio/*.h"
        "${_src}/net/aio/*.hpp"
        "${_src}/io/readiness/*.c"
        "${_src}/io/readiness/*.cpp"
        "${_src}/io/readiness/*.h"
        "${_src}/io/readiness/*.hpp")

foreach (_file IN LISTS _scan)
    file(READ "${_file}" _contents)
    string(FIND "${_contents}" "ASTIA_AIO_REGISTER_" _offset)
    if (NOT _offset EQUAL -1)
        message(FATAL_ERROR "AIO registration macro remains in ${_file}")
    endif ()
endforeach ()

file(GLOB_RECURSE _test_scan
        "${ASTIA_KERNEL_SOURCE_DIR}/tests/aio_*.c"
        "${ASTIA_KERNEL_SOURCE_DIR}/tests/aio_*.cpp"
        "${ASTIA_KERNEL_SOURCE_DIR}/tests/aio_*.h"
        "${ASTIA_KERNEL_SOURCE_DIR}/tests/aio_*.hpp"
        "${ASTIA_KERNEL_SOURCE_DIR}/tests/support/aio_*.h"
        "${ASTIA_KERNEL_SOURCE_DIR}/tests/support/aio_*.hpp")
foreach (_file IN LISTS _test_scan)
    file(READ "${_file}" _contents)
    string(FIND "${_contents}" "ASTIA_AIO_HOST_TEST" _host_offset)
    if (NOT _host_offset EQUAL -1)
        message(FATAL_ERROR "host-only AIO test bypass remains in ${_file}")
    endif ()
    if (_contents MATCHES
            "filesystem::dispatch|socket::dispatch|readiness::dispatch|dispatch::dispatch_core")
        message(FATAL_ERROR "AIO tests must not stub canonical handler entry points: ${_file}")
    endif ()
endforeach ()

file(READ "${ASTIA_KERNEL_SOURCE_DIR}/tests/CMakeLists.txt" _aio_test_cmake)
if (NOT _aio_test_cmake MATCHES "ASTIA_AIO_HANDLER_TEST_SOURCES")
    message(FATAL_ERROR "AIO tests no longer link owner handler translation units")
endif ()
if (NOT _aio_test_cmake MATCHES "src/io/aio/dispatch.cpp")
    message(FATAL_ERROR "AIO tests no longer link the shared production dispatch core")
endif ()

file(READ "${_aio}/dispatch.cpp" _core_dispatch)
foreach (_shared
        file_for resolve_at parent_writable submit_socket_request submit_io
        publish_pending retry_pending resume_parked complete_control poll_now_ns
        dispatch_one)
    string(FIND "${_core_dispatch}" "${_shared}" _shared_offset)
    if (_shared_offset EQUAL -1)
        message(FATAL_ERROR "shared AIO core helper was removed: ${_shared}")
    endif ()
endforeach ()

file(READ "${_aio}/aio.cpp" _aio_runtime)
foreach (_caller retry_pending resume_parked complete_control dispatch_one poll_now_ns)
    string(FIND "${_aio_runtime}" "dispatch::${_caller}" _caller_offset)
    if (_caller_offset EQUAL -1)
        message(FATAL_ERROR
                "task-context AIO path no longer exercises shared core: ${_caller}")
    endif ()
endforeach ()

file(READ "${_aio}/registry.cpp" _registry)
if (_registry MATCHES "filesystem|readiness|socket|aio_dispatch")
    message(FATAL_ERROR "generic AIO registry imports concrete owner headers")
endif ()

list(LENGTH _scan _scan_count)
message(STATUS
        "AIO handler ownership: 43 descriptors across 4 owner directories; production bodies are owner-local; shared submission/publication core retained; no registration macros (${_scan_count} files)")
