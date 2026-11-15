# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Arlirad

set(_astia_root "${CMAKE_CURRENT_LIST_DIR}/..")

file(READ "${_astia_root}/LICENSE" _astia_license)
file(READ "${_astia_root}/README.md" _astia_readme)
file(READ "${_astia_root}/CMakeLists.txt" _astia_cmake)

foreach(_astia_text _astia_license _astia_readme _astia_cmake)
    if(NOT "${${_astia_text}}" MATCHES "SPDX-License-Identifier: MIT|MIT License")
        message(FATAL_ERROR "${_astia_text} does not identify the MIT license")
    endif()
    if(NOT "${${_astia_text}}" MATCHES "Copyright \\(c\\) 2026 Arlirad")
        message(FATAL_ERROR "${_astia_text} does not identify Arlirad's copyright")
    endif()
endforeach()

foreach(_astia_required
        "Permission is hereby granted, free of charge"
        "The above copyright notice and this permission notice shall be included"
        "THE SOFTWARE IS PROVIDED \"AS IS\"")
    if(NOT "${_astia_license}" MATCHES "${_astia_required}")
        message(FATAL_ERROR "LICENSE is missing canonical MIT text: ${_astia_required}")
    endif()
endforeach()
