find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(ASTIA_VERSION_ARTIFACT "${CMAKE_BINARY_DIR}/generated/astia-version.json")
set(_astia_version_command
    "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/../tools/astia_version.py"
    --root "${CMAKE_SOURCE_DIR}" --output "${ASTIA_VERSION_ARTIFACT}")
if(DEFINED ASTIA_VERSION_OVERRIDE AND NOT ASTIA_VERSION_OVERRIDE STREQUAL "")
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
foreach(_field version major minor patch)
    string(JSON ASTIA_VERSION_${_field} GET "${_astia_version_json}" ${_field})
endforeach()
set(ASTIA_VERSION "${ASTIA_VERSION_version}")
set(ASTIA_VERSION_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated/include")
file(MAKE_DIRECTORY "${ASTIA_VERSION_INCLUDE_DIR}/astia")
file(WRITE "${ASTIA_VERSION_INCLUDE_DIR}/astia/version.hpp"
    "#pragma once\n#define ASTIA_VERSION_MAJOR ${ASTIA_VERSION_major}\n#define ASTIA_VERSION_MINOR ${ASTIA_VERSION_minor}\n#define ASTIA_VERSION_PATCH ${ASTIA_VERSION_patch}\n#define ASTIA_VERSION_STRING \"${ASTIA_VERSION}\"\n")
file(WRITE "${ASTIA_VERSION_INCLUDE_DIR}/astia/version.h"
    "#ifndef ASTIA_VERSION_H\n#define ASTIA_VERSION_H\n#define ASTIA_VERSION_MAJOR ${ASTIA_VERSION_major}\n#define ASTIA_VERSION_MINOR ${ASTIA_VERSION_minor}\n#define ASTIA_VERSION_PATCH ${ASTIA_VERSION_patch}\n#define ASTIA_VERSION_STRING \"${ASTIA_VERSION}\"\n#endif\n")
