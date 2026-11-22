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
        string(REGEX MATCHALL "_descriptor =" _matches "${_contents}")
        list(LENGTH _matches _count)
        if (NOT _count EQUAL 1)
            message(FATAL_ERROR
                    "${_file}: expected exactly one descriptor, found ${_count}")
        endif ()
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

file(READ "${_aio}/registry.cpp" _registry)
if (_registry MATCHES "filesystem|readiness|socket|aio_dispatch")
    message(FATAL_ERROR "generic AIO registry imports concrete owner headers")
endif ()

list(LENGTH _scan _scan_count)
message(STATUS
        "AIO handler ownership: 43 descriptors across 4 owner directories; no registration macros (${_scan_count} files)")
