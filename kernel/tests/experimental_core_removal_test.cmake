if (NOT DEFINED ASTIA_REPO_ROOT)
    message(FATAL_ERROR "ASTIA_REPO_ROOT is required")
endif ()

set(removed_reference_pattern "fib(er|re)")
set(scan_self ":(exclude)kernel/tests/experimental_core_removal_test.cmake")

execute_process(
        COMMAND git -C "${ASTIA_REPO_ROOT}" grep -n -i -E
                "${removed_reference_pattern}" -- . "${scan_self}"
        RESULT_VARIABLE scan_result
        OUTPUT_VARIABLE scan_output
        ERROR_VARIABLE scan_error)
if (scan_result EQUAL 0)
    message(FATAL_ERROR
            "tracked live references to the removed experimental core remain:\n"
            "${scan_output}")
endif ()
if (NOT scan_result EQUAL 1)
    message(FATAL_ERROR
            "tracked-reference scan failed (git grep exit ${scan_result}):\n"
            "${scan_error}")
endif ()
