if(DEFINED ASTIA_VERSION AND NOT "${ASTIA_VERSION}" STREQUAL "")
    return()
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(ASTIA_VERSION_OVERRIDE "$ENV{ASTIA_VERSION_OVERRIDE}" CACHE STRING
        "Explicit SemVer version used instead of Git-derived metadata")
option(ASTIA_VERSION_REQUIRE_RELEASE
        "Fail configuration unless the version is a clean exact release tag" OFF)

set(ASTIA_VERSION_ARTIFACT "${CMAKE_BINARY_DIR}/generated/astia-version.json"
        CACHE INTERNAL "Derived Astia version metadata")
set(ASTIA_VERSION_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated/include"
        CACHE INTERNAL "Generated Astia version headers")

set(_astia_version_command
        "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/../tools/astia_version.py"
        --root "${CMAKE_SOURCE_DIR}" --output "${ASTIA_VERSION_ARTIFACT}")
if(NOT "${ASTIA_VERSION_OVERRIDE}" STREQUAL "")
    list(APPEND _astia_version_command --override "${ASTIA_VERSION_OVERRIDE}")
endif()
if(ASTIA_VERSION_REQUIRE_RELEASE)
    list(APPEND _astia_version_command --reject-dirty)
endif()

execute_process(COMMAND ${_astia_version_command}
        RESULT_VARIABLE _astia_version_result
        ERROR_VARIABLE _astia_version_error)
if(NOT _astia_version_result EQUAL 0)
    message(FATAL_ERROR "Unable to derive Astia version: ${_astia_version_error}")
endif()

file(READ "${ASTIA_VERSION_ARTIFACT}" _astia_version_json)
foreach(_field version package_version major minor patch sha source)
    string(JSON ASTIA_VERSION_${_field} GET "${_astia_version_json}" ${_field})
endforeach()
foreach(_field dirty release)
    string(JSON ASTIA_VERSION_${_field} GET "${_astia_version_json}" ${_field})
    if(ASTIA_VERSION_${_field})
        set(ASTIA_VERSION_${_field} 1)
    else()
        set(ASTIA_VERSION_${_field} 0)
    endif()
endforeach()

file(MAKE_DIRECTORY "${ASTIA_VERSION_INCLUDE_DIR}/astia")
file(WRITE "${ASTIA_VERSION_INCLUDE_DIR}/astia/version.h"
        "#ifndef ASTIA_VERSION_H\n"
        "#define ASTIA_VERSION_H\n"
        "#define ASTIA_VERSION_MAJOR ${ASTIA_VERSION_major}\n"
        "#define ASTIA_VERSION_MINOR ${ASTIA_VERSION_minor}\n"
        "#define ASTIA_VERSION_PATCH ${ASTIA_VERSION_patch}\n"
        "#define ASTIA_VERSION_STRING \"${ASTIA_VERSION_version}\"\n"
        "#define ASTIA_VERSION_PACKAGE_STRING \"${ASTIA_VERSION_package_version}\"\n"
        "#define ASTIA_VERSION_SHA \"${ASTIA_VERSION_sha}\"\n"
        "#define ASTIA_VERSION_DIRTY ${ASTIA_VERSION_dirty}\n"
        "#define ASTIA_VERSION_RELEASE ${ASTIA_VERSION_release}\n"
        "#endif\n")
file(WRITE "${ASTIA_VERSION_INCLUDE_DIR}/astia/version.hpp"
        "#pragma once\n"
        "\n"
        "#include <astia/version.h>\n")

set(ASTIA_PACKAGE_VERSION "${ASTIA_VERSION_package_version}" CACHE INTERNAL
        "Version embedded in Astia package metadata")
