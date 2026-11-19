function(astia_append_test_label label)
    get_property(_astia_tests DIRECTORY PROPERTY TESTS)
    foreach(_astia_test IN LISTS _astia_tests)
        get_property(_astia_labels TEST ${_astia_test} PROPERTY LABELS)
        if(_astia_labels)
            list(APPEND _astia_labels ${label})
        else()
            set(_astia_labels ${label})
        endif()
        list(REMOVE_DUPLICATES _astia_labels)
        set_tests_properties(${_astia_test}
                PROPERTIES LABELS "${_astia_labels}")
    endforeach()
endfunction()

function(astia_add_test_label name label)
    get_property(_astia_labels TEST ${name} PROPERTY LABELS)
    if(_astia_labels)
        list(APPEND _astia_labels ${label})
    else()
        set(_astia_labels ${label})
    endif()
    list(REMOVE_DUPLICATES _astia_labels)
    set_tests_properties(${name} PROPERTIES LABELS "${_astia_labels}")
endfunction()

function(astia_register_script_test name label script)
    set(options EXTERNAL MANUAL)
    set(one_value_args TIMEOUT WORKING_DIRECTORY)
    set(multi_value_args ENV ARGS)
    cmake_parse_arguments(ASTIA_SCRIPT "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    add_test(NAME ${name}
            COMMAND ${CMAKE_COMMAND} -E env ${ASTIA_SCRIPT_ENV}
            sh ${script} ${ASTIA_SCRIPT_ARGS})
    set_tests_properties(${name} PROPERTIES
            LABELS "${label}"
            TIMEOUT "${ASTIA_SCRIPT_TIMEOUT}")
    if(";${label};" MATCHES ";qemu;")
        set_tests_properties(${name} PROPERTIES
                RESOURCE_LOCK astia-qemu)
    endif()
    if(ASTIA_SCRIPT_WORKING_DIRECTORY)
        set_tests_properties(${name} PROPERTIES
                WORKING_DIRECTORY "${ASTIA_SCRIPT_WORKING_DIRECTORY}")
    endif()
    if(ASTIA_SCRIPT_EXTERNAL AND NOT ASTIA_ENABLE_EXTERNAL_TESTS)
        set_tests_properties(${name} PROPERTIES DISABLED TRUE)
    endif()
    if(ASTIA_SCRIPT_MANUAL AND NOT ASTIA_ENABLE_MANUAL_TESTS)
        set_tests_properties(${name} PROPERTIES DISABLED TRUE)
    endif()
endfunction()
