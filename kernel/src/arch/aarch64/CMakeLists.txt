if (ARCH STREQUAL "aarch64")
    add_library(arch_aarch64 OBJECT
            exception.S
            start.S
            page_map.cpp
    )
    target_include_directories(arch_aarch64 PUBLIC
            ${ASTIA_KERNEL_SOURCE_DIR}/include)
endif ()

if (ASTIA_EXPERIMENTAL_AARCH64)
set(_astia_aarch64_experimental_dir
        "${CMAKE_CURRENT_BINARY_DIR}/experimental-aarch64")
set(_astia_aarch64_experimental_sources
        port.cpp
        pci_config.cpp)

set(ASTIA_AARCH64_CXX_COMPILER "" CACHE FILEPATH
        "Cross C++ compiler for the compile-only AArch64 bring-up target")
if (NOT ASTIA_AARCH64_CXX_COMPILER)
    find_program(_astia_aarch64_cxx_compiler
            NAMES aarch64-linux-gnu-g++ aarch64-none-elf-g++
            DOC "Cross C++ compiler for the compile-only AArch64 bring-up target")
    if (_astia_aarch64_cxx_compiler)
        set(ASTIA_AARCH64_CXX_COMPILER "${_astia_aarch64_cxx_compiler}"
                CACHE FILEPATH
                "Cross C++ compiler for the compile-only AArch64 bring-up target"
                FORCE)
    endif ()
endif ()
if (NOT ASTIA_AARCH64_CXX_COMPILER)
    message(FATAL_ERROR
            "ASTIA_EXPERIMENTAL_AARCH64 requires an AArch64 C++ compiler; "
            "set ASTIA_AARCH64_CXX_COMPILER explicitly")
endif ()

set(_astia_aarch64_experimental_objects)
foreach (_source IN LISTS _astia_aarch64_experimental_sources)
    get_filename_component(_name "${_source}" NAME_WE)
    set(_object "${_astia_aarch64_experimental_dir}/${_name}.o")
    add_custom_command(
            OUTPUT "${_object}"
            COMMAND ${CMAKE_COMMAND} -E make_directory
                    "${_astia_aarch64_experimental_dir}"
            COMMAND "${ASTIA_AARCH64_CXX_COMPILER}"
                    -std=c++20
                    -ffreestanding
                    -fno-exceptions
                    -fno-rtti
                    -fno-builtin
                    -fno-pie
                    -fno-pic
                    -fno-stack-protector
                    -mno-outline-atomics
                    -I${ASTIA_KERNEL_SOURCE_DIR}/include
                    -I${ASTIA_KERNEL_SOURCE_DIR}/src/arch
                    -c "${CMAKE_CURRENT_SOURCE_DIR}/${_source}"
                    -o "${_object}"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${_source}"
            COMMENT "Compiling experimental AArch64 ${_name}.cpp"
            VERBATIM)
    list(APPEND _astia_aarch64_experimental_objects "${_object}")
endforeach ()

add_custom_target(astia_experimental_aarch64
        DEPENDS ${_astia_aarch64_experimental_objects})
set_property(TARGET astia_experimental_aarch64 PROPERTY EXCLUDE_FROM_ALL TRUE)
endif ()
