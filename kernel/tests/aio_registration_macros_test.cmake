if (NOT DEFINED ASTIA_KERNEL_SOURCE_DIR)
    message(FATAL_ERROR "ASTIA_KERNEL_SOURCE_DIR is required")
endif ()

set(_aio_dir "${ASTIA_KERNEL_SOURCE_DIR}/src/io/aio")
if (EXISTS "${_aio_dir}/operation_registration.hpp")
    message(FATAL_ERROR
            "AIO registration header must not exist: ${_aio_dir}/operation_registration.hpp")
endif ()

file(GLOB_RECURSE _aio_sources
        "${_aio_dir}/*.c"
        "${_aio_dir}/*.cpp"
        "${_aio_dir}/*.h"
        "${_aio_dir}/*.hpp")

foreach (_source IN LISTS _aio_sources)
    file(READ "${_source}" _contents)
    string(FIND "${_contents}" "ASTIA_AIO_REGISTER_" _offset)
    if (NOT _offset EQUAL -1)
        message(FATAL_ERROR "AIO registration macro remains in ${_source}")
    endif ()
endforeach ()

list(LENGTH _aio_sources _source_count)
message(STATUS "AIO registration macros: none (${_source_count} files)")
