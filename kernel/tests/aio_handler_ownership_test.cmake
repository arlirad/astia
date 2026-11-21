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

set(_owners
        "${_aio}/core_handlers.cpp|15"
        "${_src}/fs/aio_handlers.cpp|15"
        "${_src}/net/aio_handlers.cpp|11"
        "${_src}/io/readiness_handlers.cpp|2")

foreach (_entry IN LISTS _owners)
    string(REPLACE "|" ";" _parts "${_entry}")
    list(GET _parts 0 _file)
    list(GET _parts 1 _expected)
    if (NOT EXISTS "${_file}")
        message(FATAL_ERROR "missing owner handler file: ${_file}")
    endif ()
    file(READ "${_file}" _contents)
    string(REGEX MATCHALL "_descriptor =" _matches "${_contents}")
    list(LENGTH _matches _count)
    if (NOT _count EQUAL _expected)
        message(FATAL_ERROR
                "${_file}: expected ${_expected} descriptors, found ${_count}")
    endif ()
endforeach ()

file(GLOB_RECURSE _scan
        "${_aio}/*.c"
        "${_aio}/*.cpp"
        "${_aio}/*.h"
        "${_aio}/*.hpp"
        "${_src}/fs/aio_*.c"
        "${_src}/fs/aio_*.cpp"
        "${_src}/fs/aio_*.h"
        "${_src}/fs/aio_*.hpp"
        "${_src}/net/aio_*.c"
        "${_src}/net/aio_*.cpp"
        "${_src}/net/aio_*.h"
        "${_src}/net/aio_*.hpp"
        "${_src}/io/readiness*.c"
        "${_src}/io/readiness*.cpp"
        "${_src}/io/readiness*.h"
        "${_src}/io/readiness*.hpp")

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
        "AIO handler ownership: 43 descriptors across 4 owner files; no registration macros (${_scan_count} files)")
