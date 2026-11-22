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
        "${_src}/io/readiness/dispatch.cpp")
    if (EXISTS "${_retired_dispatch}")
        message(FATAL_ERROR
                "aggregate handler dispatch must not remain: ${_retired_dispatch}")
    endif ()
endforeach ()

set(_enosys_handlers spawn wait)

function(astia_aio_check_handler _file _stem)
    file(STRINGS "${_file}" _lines)
    set(_host_guard FALSE)
    set(_mode normal)
    set(_production "")
    foreach (_line IN LISTS _lines)
        if (_line MATCHES "^[ \t]*#ifdef[ \t]+ASTIA_AIO_HOST_TEST")
            set(_host_guard TRUE)
            set(_mode skip_host)
        elseif (_line MATCHES "^[ \t]*#ifndef[ \t]+ASTIA_AIO_HOST_TEST")
            set(_mode include_prod)
        elseif (_mode STREQUAL "skip_host" AND _line MATCHES "^[ \t]*#else")
            set(_mode include_alt)
        elseif ((_mode STREQUAL "include_alt" OR _mode STREQUAL "include_prod")
                AND _line MATCHES "^[ \t]*#endif")
            set(_mode normal)
        elseif (_mode STREQUAL "normal" OR _mode STREQUAL "include_alt"
                OR _mode STREQUAL "include_prod")
            string(APPEND _production "${_line}\n")
        endif ()
    endforeach ()

    if (NOT _host_guard)
        message(FATAL_ERROR "${_file}: missing explicit ASTIA_AIO_HOST_TEST route seam")
    endif ()
    if (_production MATCHES
            "filesystem::dispatch|socket::dispatch|readiness::dispatch|dispatch::dispatch_core")
        message(FATAL_ERROR "${_file}: production path is a trivial forwarder")
    endif ()
    if (NOT _production MATCHES "return ")
        message(FATAL_ERROR "${_file}: production path has no implementation body")
    endif ()
    if (_stem IN_LIST _enosys_handlers AND NOT _production MATCHES "-ENOSYS")
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
