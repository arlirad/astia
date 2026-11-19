if(NOT DEFINED ASTIA_MODE OR NOT DEFINED ASTIA_OUTPUT)
    message(FATAL_ERROR "warning regression test requires ASTIA_MODE and ASTIA_OUTPUT")
endif()

get_filename_component(output_directory "${ASTIA_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")

if(ASTIA_MODE STREQUAL "kernel")
    if(NOT DEFINED ASTIA_COMPILE_COMMANDS)
        message(FATAL_ERROR "kernel warning regression requires compile_commands.json")
    endif()
    file(READ "${ASTIA_COMPILE_COMMANDS}" commands)
    if(NOT commands MATCHES "astiakrn\\.dir/" OR NOT commands MATCHES "-Werror")
        message(FATAL_ERROR "production kernel compile command is not warning-as-error")
    endif()
elseif(ASTIA_MODE STREQUAL "module_cxx")
    execute_process(
            COMMAND "${ASTIA_CXX_COMPILER}" -O2 -std=c++20 -ffreestanding -fno-builtin
                    -fno-pic -fno-pie -fno-exceptions -fno-rtti -fno-threadsafe-statics
                    -fno-stack-protector -fno-omit-frame-pointer -mno-red-zone
                    -mcmodel=large -fno-asynchronous-unwind-tables -Wall -Wextra -Werror
                    -pedantic "-I${ASTIA_KERNEL_INCLUDE}" -c "${ASTIA_SOURCE}" -o "${ASTIA_OUTPUT}"
            RESULT_VARIABLE result
            OUTPUT_VARIABLE output
            ERROR_VARIABLE error)
    if(result)
        message(FATAL_ERROR "C++ module warning regression failed:\n${output}${error}")
    endif()
else()
    message(FATAL_ERROR "unknown warning regression mode: ${ASTIA_MODE}")
endif()
