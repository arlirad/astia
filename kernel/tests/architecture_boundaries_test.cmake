if (NOT DEFINED ASTIA_KERNEL_SOURCE_DIR OR
        NOT DEFINED ASTIA_ARCHITECTURE_BOUNDARIES_SCRIPT OR
        NOT DEFINED ASTIA_PYTHON)
    message(FATAL_ERROR
            "ASTIA_KERNEL_SOURCE_DIR, ASTIA_ARCHITECTURE_BOUNDARIES_SCRIPT, "
            "and ASTIA_PYTHON are required")
endif ()

execute_process(
        COMMAND "${ASTIA_PYTHON}" "${ASTIA_ARCHITECTURE_BOUNDARIES_SCRIPT}"
                --root "${ASTIA_KERNEL_SOURCE_DIR}"
        RESULT_VARIABLE current_result
        OUTPUT_VARIABLE current_output
        ERROR_VARIABLE current_error)
if (current_result)
    message(FATAL_ERROR
            "current source tree violates the dependency contract:\n"
            "${current_output}${current_error}")
endif ()

set(fixture_root "${CMAKE_CURRENT_BINARY_DIR}/architecture-boundary-fixtures")
file(REMOVE_RECURSE "${fixture_root}")

function(assert_fixture_rejected name expected_text)
    execute_process(
            COMMAND "${ASTIA_PYTHON}" "${ASTIA_ARCHITECTURE_BOUNDARIES_SCRIPT}"
                    --root "${fixture_root}/${name}"
            RESULT_VARIABLE fixture_result
            OUTPUT_VARIABLE fixture_output
            ERROR_VARIABLE fixture_error)
    if (fixture_result EQUAL 0)
        message(FATAL_ERROR
                "${name} fixture was accepted:\n"
                "${fixture_output}${fixture_error}")
    endif ()
    if (NOT "${fixture_output}${fixture_error}" MATCHES
            "${expected_text}")
        message(FATAL_ERROR
                "${name} fixture failed without the expected diagnostic "
                "'${expected_text}':\n${fixture_output}${fixture_error}")
    endif ()
endfunction()

file(MAKE_DIRECTORY "${fixture_root}/forbidden-include/include/astia/core")
file(WRITE "${fixture_root}/forbidden-include/include/astia/core/core.hpp"
        "#include <astia/fs.hpp>\n")
file(WRITE "${fixture_root}/forbidden-include/CMakeLists.txt"
        "add_library(core STATIC core.cpp)\n")
assert_fixture_rejected("forbidden-include" "forbidden include")

file(MAKE_DIRECTORY "${fixture_root}/forbidden-target/include/astia")
file(WRITE "${fixture_root}/forbidden-target/CMakeLists.txt" [=[
add_library(net STATIC net.cpp)
target_link_libraries(net PUBLIC dev)
]=])
assert_fixture_rejected("forbidden-target" "forbidden target dependency")

file(MAKE_DIRECTORY "${fixture_root}/cycle/include/astia/dev")
file(WRITE "${fixture_root}/cycle/include/astia/fs.hpp"
        "#include <astia/dev/device_file.hpp>\n")
file(WRITE "${fixture_root}/cycle/include/astia/dev/device_file.hpp"
        "#include <astia/fs.hpp>\n")
file(WRITE "${fixture_root}/cycle/CMakeLists.txt" [=[
add_library(dev STATIC device.cpp)
add_library(fs STATIC filesystem.cpp)
target_link_libraries(dev PUBLIC fs)
target_link_libraries(fs PUBLIC dev)
]=])
assert_fixture_rejected("cycle" "dependency cycle")
